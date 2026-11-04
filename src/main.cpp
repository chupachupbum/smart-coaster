/**
 * @file main.cpp
 * @brief Smart Cup Stand - System Firmware Coordinator & Cooperative Multi-Rate Scheduler
 *
 * Hardware Target: ESP32-C3 DevKitM-1 (Wokwi Simulation & Hardware)
 * PlatformIO Environment: esp32-c3-devkitm-1
 *
 * Architecture & Rate Budget:
 * - Loop Tick (Continuous, ~0ms): UART RX stream polling & non-blocking CLI parser
 * - 50 Hz (20 ms): Status LED PWM sinusoidal breathing & celebration pattern engine
 * - 20 Hz (50 ms): Weight sensor sampling, domain state machine update, and event pump
 * - 10 Hz (100 ms): SSD1306 128x64 OLED display frame rendering (400kHz Fast I2C)
 * - 20 Hz (50 ms): Network push state machine tick & ntfy.sh HTTP client dispatch
 * - 0.2 Hz (5000 ms): Periodic structured JSON telemetry heartbeat
 *
 * Invariants:
 * - 100% rollover-safe 32-bit unsigned modulo subtraction arithmetic
 * - Zero dynamic heap allocation in steady-state loop
 * - Safe GPIO pinout avoiding strapping pins (GPIO 2, 8, 9)
 * - Non-blocking FreeRTOS watchdog compliance via cooperative delay(1)
 */

#include <Arduino.h>
#include <Wire.h>

#include "pins_config.h"
#include "config.h"
#include "coaster_domain.h"
#include "hal/weight_sensor.h"
#include "hal/display_oled.h"
#include "hal/status_led.h"
#include "telemetry/telemetry_console.h"
#include "net/network_push.h"

// Configuration Fallbacks & Compatibility Aliases
#ifndef CALIBRATION_SCALE_FACTOR
#define CALIBRATION_SCALE_FACTOR HX711_SCALE_FACTOR
#endif

#ifndef WIFI_PASS
#define WIFI_PASS WIFI_PASSWORD
#endif

#ifndef NTFY_TOPIC
#define NTFY_TOPIC NTFY_DEFAULT_TOPIC
#endif

// ============================================================================
// Global Subsystem Instances
// ============================================================================
static CoasterDomain     domain(DEFAULT_DAILY_GOAL_ML);
static WeightSensor      weight_sensor;
static DisplayOled       display(&Wire);
static StatusLed         status_led(PIN_STATUS_LED, LEDC_PWM_CHANNEL, LEDC_PWM_FREQ_HZ, LEDC_PWM_RESOLUTION);
static TelemetryConsole  telemetry(TELEMETRY_HEARTBEAT_INTERVAL_MS);
static NetworkPush       network;

// ============================================================================
// Cooperative Scheduler Rate Budget (Intervals in Milliseconds)
// ============================================================================
static constexpr uint32_t INTERVAL_STATUS_LED_MS    = 20UL;   // 50 Hz: PWM breathing & patterns
static constexpr uint32_t INTERVAL_SENSOR_DOMAIN_MS = 50UL;   // 20 Hz: Weight sampling & domain evaluation
static constexpr uint32_t INTERVAL_NETWORK_PUSH_MS  = 50UL;   // 20 Hz: Wi-Fi FSM & push notification queue
static constexpr uint32_t INTERVAL_DISPLAY_OLED_MS  = 100UL;  // 10 Hz: SSD1306 OLED UI rendering

// ============================================================================
// Scheduler Timestamps (Rollover-Safe 32-Bit Millis Trackers)
// ============================================================================
static uint32_t last_led_ms           = 0;
static uint32_t last_sensor_domain_ms = 0;
static uint32_t last_net_ms           = 0;
static uint32_t last_display_ms       = 0;

// State Change Tracking (for un-evented state transitions)
static CupState   last_cup_state       = CupState::EMPTY;
static AlertState last_alert_state     = AlertState::IDLE;
static bool       last_tick_had_events = false;

// ============================================================================
// Domain Event Pump & Peripheral Routing
// ============================================================================
/**
 * @brief Central event pump: drains queued domain events and dispatches across subsystems.
 *
 * Routing Responsibilities:
 * - TelemetryConsole: Emits single-line JSON telemetry for every domain event.
 * - StatusLed: Triggers celebratory triple-flash pattern on DRINK_LOGGED.
 * - DisplayOled: Triggers celebratory splash banner and forces immediate redraw on DRINK_LOGGED / REMINDER_ALERT.
 * - NetworkPush: Queues HTTP POST push notifications for drink, reminder, repeat, and goal events.
 *
 * @param now_ms Current system timestamp in milliseconds.
 */
static void dispatchDomainEvents(uint32_t now_ms) {
    last_tick_had_events = false;

    while (domain.has_events()) {
        last_tick_had_events = true;
        DomainEvent ev = domain.pop_event();
        const CoasterStatus& status = domain.get_status();
        const char* evt_str = CoasterDomain::event_type_to_string(ev.type);

        // 1. Telemetry emission over UART
        telemetry.emitTelemetry(Serial, status, evt_str, now_ms);

        // 2. Peripheral routing based on event type
        switch (ev.type) {
            case DomainEventType::DRINK_LOGGED:
                // Visual celebration: LED triple flash + OLED "+X ml consumed!" banner
                status_led.triggerDrinkLoggedFlash(now_ms);
                display.triggerDrinkSplash(ev.value1, now_ms);
                // Force immediate OLED update to bypass 10Hz throttle
                display.render(status, network.isConnected(), now_ms, true);
                // Dispatch push notification to ntfy.sh
                network.handleDomainEvent(ev, status);
                break;

            case DomainEventType::REMINDER_ALERT:
                // Primary inactivity reminder: queue push notification & force OLED banner
                network.handleDomainEvent(ev, status);
                display.render(status, network.isConnected(), now_ms, true);
                break;

            case DomainEventType::REPEAT_ALERT:
                // Repeating hydration reminder: queue repeat push notification
                network.handleDomainEvent(ev, status);
                display.render(status, network.isConnected(), now_ms, true);
                break;

            case DomainEventType::GOAL_REACHED:
                // Daily goal achieved: queue congratulatory push notification
                network.handleDomainEvent(ev, status);
                break;

            case DomainEventType::REFILL_DETECTED:
            case DomainEventType::AUTO_TARE_APPLIED:
            case DomainEventType::LIFT_NO_DRINK:
            case DomainEventType::STATE_CHANGE:
            case DomainEventType::CUP_PLACED:
            case DomainEventType::CUP_LIFTED:
            default:
                // Telemetry already emitted; no peripheral action required
                break;
        }
    }
}

/**
 * @brief Catch and emit telemetry on state changes that occur without explicit domain events.
 *
 * Examples: manual tare, CLI RESET, or alert returning to IDLE when coaster is cleared.
 * Ensures 100% telemetry synchronization between firmware state and external observers.
 *
 * @param now_ms Current system timestamp in milliseconds.
 */
static void checkStateTransitions(uint32_t now_ms) {
    const CoasterStatus& status = domain.get_status();
    bool state_changed = false;

    if (status.cup_state != last_cup_state) {
        state_changed = true;
        last_cup_state = status.cup_state;
    }

    if (status.alert_state != last_alert_state) {
        state_changed = true;
        last_alert_state = status.alert_state;
    }

    // Only emit synthetic STATE_CHANGE telemetry if the domain event queue did not already
    // emit a domain event during this tick, avoiding duplicate packets.
    if (state_changed && !last_tick_had_events) {
        telemetry.emitTelemetry(Serial, status, "STATE_CHANGE", now_ms);
    }
}

// ============================================================================
// System Boot & Peripheral Initialization
// ============================================================================
void setup() {
    // 1. Hardware UART0: Resize TX buffer to 512B *before* begin() to prevent blocking
    Serial.setTxBufferSize(512);
    Serial.begin(SERIAL_BAUD_RATE);
    delay(50); // Transient UART voltage stabilization

    Serial.println(F("\n=================================================="));
    Serial.println(F("  ESP32-C3 SMART CUP STAND - SYSTEM INTEGRATION"));
    Serial.println(F("=================================================="));

    // 2. Safe I2C Bus Initialization: GPIO 4 (SDA), GPIO 5 (SCL) at 400kHz Fast-Mode
    // Avoids default Arduino Wire pins (GPIO 8/9 strapping pins)
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    Wire.setClock(400000);

    // 3. SSD1306 OLED UI Display Initialization
    if (display.begin(OLED_I2C_ADDR, &Wire)) {
        display.showBootScreen("SMART CUP STAND", "Initializing...");
        Serial.println(F("[BOOT] SSD1306 OLED display initialized @ 0x3C (400kHz)"));
    } else {
        Serial.println(F("[WARN] SSD1306 OLED not detected @ 0x3C; continuing in headless mode"));
    }

    // 4. Status LED Initialization (LEDC PWM on GPIO 3, 5kHz, 8-bit)
    status_led.begin(PIN_STATUS_LED, LEDC_PWM_CHANNEL, LEDC_PWM_FREQ_HZ, LEDC_PWM_RESOLUTION);
    status_led.update(0, CupState::EMPTY, AlertState::IDLE);
    Serial.println(F("[BOOT] Status LED initialized on GPIO 3 (5kHz PWM)"));

    // 5. HX711 Weight Sensor Interface: GPIO 6 (DT), GPIO 7 (SCK), 0.42 counts/g
    weight_sensor.begin(PIN_HX711_DT, PIN_HX711_SCK, CALIBRATION_SCALE_FACTOR);
    Serial.println(F("[BOOT] HX711 weight sensor initialized on GPIO 6/7"));

    // Bounded non-blocking weight sensor priming (<= 200ms timeout)
    // Prevents hanging in setup() if transducer is disconnected or floating
    uint32_t hx_start_ms = millis();
    bool initial_reading_ready = false;
    while (millis() - hx_start_ms < 200) {
        if (weight_sensor.isReady()) {
            float initial_g = weight_sensor.readGrams();
            domain.update(initial_g, millis());
            initial_reading_ready = true;
            Serial.print(F("[BOOT] Initial weight sample acquired: "));
            Serial.print(initial_g, 1);
            Serial.println(F(" g"));
            break;
        }
        delay(10); // Cooperative yield during boot wait
    }
    if (!initial_reading_ready) {
        Serial.println(F("[BOOT] HX711 conversion pending; deferred to cooperative loop"));
    }

    // 6. Network Push Client & Wi-Fi Initialization
    network.begin(WIFI_SSID, WIFI_PASS, NTFY_TOPIC, NTFY_SERVER_URL);
    Serial.print(F("[BOOT] Wi-Fi client started for SSID: "));
    Serial.println(WIFI_SSID);

    // 7. Telemetry Console & Initial SYSTEM_BOOT Emission
    telemetry.begin();
    telemetry.setWifiStatus(network.getWifiStateStr());

    const uint32_t boot_now_ms = millis();
    telemetry.emitTelemetry(Serial, domain.get_status(), "SYSTEM_BOOT", boot_now_ms);

    // Synchronize initial state baselines
    last_cup_state   = domain.get_status().cup_state;
    last_alert_state = domain.get_status().alert_state;

    // Synchronize scheduler baseline timestamps
    last_led_ms           = boot_now_ms;
    last_sensor_domain_ms = boot_now_ms;
    last_net_ms           = boot_now_ms;
    last_display_ms       = boot_now_ms;

    Serial.println(F("[BOOT] Initialization complete. Entering scheduler loop.\n"));
}

// ============================================================================
// Main Cooperative Multi-Rate Scheduler Loop
// ============================================================================
void loop() {
    const uint32_t now_ms = millis();

    // ------------------------------------------------------------------------
    // Task 1: Non-Blocking UART CLI Processing (Executes Every Loop Tick)
    // ------------------------------------------------------------------------
    telemetry.processUart(Serial, domain, now_ms);

    // Keep StatusLed breathing rate in sync with domain DEMO mode
    status_led.setDemoMode(domain.is_demo_mode());

    // ------------------------------------------------------------------------
    // Task 2: 20 Hz (50 ms) - Weight Sensor Sampling & Domain Event Pump
    // ------------------------------------------------------------------------
    if (static_cast<uint32_t>(now_ms - last_sensor_domain_ms) >= INTERVAL_SENSOR_DOMAIN_MS) {
        last_sensor_domain_ms = now_ms;

        // Sample HX711 without blocking; fallback to cached weight if conversion pending
        float raw_weight_g = weight_sensor.readGrams(true);

        // Update core domain logic
        domain.update(raw_weight_g, now_ms);

        // Central Domain Event Pump: Drain and route all domain events
        dispatchDomainEvents(now_ms);

        // Catch any un-evented state transitions (e.g. CLI reset)
        checkStateTransitions(now_ms);
    }

    // ------------------------------------------------------------------------
    // Task 3: 50 Hz (20 ms) - Status LED PWM Breathing & Pattern Engine
    // ------------------------------------------------------------------------
    if (static_cast<uint32_t>(now_ms - last_led_ms) >= INTERVAL_STATUS_LED_MS) {
        last_led_ms = now_ms;

        const CoasterStatus& status = domain.get_status();
        status_led.update(now_ms, status.cup_state, status.alert_state);
    }

    // ------------------------------------------------------------------------
    // Task 4: 10 Hz (100 ms) - SSD1306 OLED UI Render
    // ------------------------------------------------------------------------
    if (static_cast<uint32_t>(now_ms - last_display_ms) >= INTERVAL_DISPLAY_OLED_MS) {
        last_display_ms = now_ms;

        const CoasterStatus& status = domain.get_status();
        display.render(status, network.isConnected(), now_ms);
    }

    // ------------------------------------------------------------------------
    // Task 5: 20 Hz (50 ms) - Wi-Fi State Machine & Push Notification Queue
    // ------------------------------------------------------------------------
    if (static_cast<uint32_t>(now_ms - last_net_ms) >= INTERVAL_NETWORK_PUSH_MS) {
        last_net_ms = now_ms;

        network.update(now_ms);
        telemetry.setWifiStatus(network.getWifiStateStr());
    }

    // ------------------------------------------------------------------------
    // Task 6: Periodic Telemetry Heartbeat (5000 ms if no events emitted)
    // ------------------------------------------------------------------------
    telemetry.update(Serial, domain.get_status(), now_ms);

    // ------------------------------------------------------------------------
    // Task 7: Cooperative Yield to FreeRTOS IDLE Task (Feeds TWDT Watchdog)
    // ------------------------------------------------------------------------
    delay(1);
}
