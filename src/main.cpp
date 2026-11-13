/**
 * @file main.cpp
 * @brief Smart Cup Stand - System Firmware Coordinator & Cooperative Multi-Rate Scheduler
 *
 * Hardware Target: ESP32-C3 DevKitM-1 (Wokwi Simulation & Hardware)
 * PlatformIO Environment: esp32-c3-devkitm-1
 *
 * Architecture & Rate Budget:
 * - Loop Tick (Continuous, ~0ms): UART RX stream polling & non-blocking CLI parser
 * - 50 Hz (20 ms): WS2812B 16-LED Ring animation & hydration status engine
 * - 20 Hz (50 ms): Weight sensor sampling, domain state machine update, and event pump
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

#include "pins_config.h"
#include "config.h"
#include "coaster_domain.h"
#include "hal/weight_sensor.h"
#include "hal/led_ring.h"
#include "telemetry/telemetry_console.h"
#include "net/network_push.h"
#include "net/wifi_portal.h"

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
static LedRing           led_ring(PIN_LED_RING, LED_RING_PIXEL_COUNT, LED_RING_MAX_BRIGHTNESS);
static TelemetryConsole  telemetry(TELEMETRY_HEARTBEAT_INTERVAL_MS);
static NetworkPush       network;
static WifiPortal        wifi_portal(domain, network, led_ring);

// ============================================================================
// Cooperative Scheduler Rate Budget (Intervals in Milliseconds)
// ============================================================================
static constexpr uint32_t INTERVAL_LED_RING_MS      = 20UL;   // 50 Hz: WS2812B 16-LED ring animations
static constexpr uint32_t INTERVAL_SENSOR_DOMAIN_MS = 50UL;   // 20 Hz: Weight sampling & domain evaluation
static constexpr uint32_t INTERVAL_NETWORK_PUSH_MS  = 50UL;   // 20 Hz: Wi-Fi FSM & push notification queue

// ============================================================================
// Scheduler Timestamps (Rollover-Safe 32-Bit Millis Trackers)
// ============================================================================
static uint32_t last_led_ms           = 0;
static uint32_t last_sensor_domain_ms = 0;
static uint32_t last_net_ms           = 0;

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
 * - LedRing: Triggers transient 16-LED ring animations (intake arc, drink ripple, refill ripple,
 *   goal rainbow) and updates ring state immediately on alerts and placement events.
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
            case DomainEventType::CUP_PLACED:
                led_ring.triggerCupPlacedArc(now_ms, status.progress_pct);
                led_ring.update(now_ms, status);
                break;

            case DomainEventType::DRINK_LOGGED:
                led_ring.triggerDrinkLogged(now_ms, status.progress_pct);
                led_ring.update(now_ms, status);
                network.handleDomainEvent(ev, status);
                break;

            case DomainEventType::GOAL_REACHED:
                led_ring.triggerGoalReached(now_ms, status.progress_pct);
                led_ring.update(now_ms, status);
                network.handleDomainEvent(ev, status);
                break;

            case DomainEventType::REFILL_DETECTED:
                led_ring.triggerRefillDetected(now_ms);
                led_ring.update(now_ms, status);
                break;

            case DomainEventType::REMINDER_ALERT:
            case DomainEventType::REPEAT_ALERT:
                network.handleDomainEvent(ev, status);
                led_ring.update(now_ms, status);
                break;

            case DomainEventType::AUTO_TARE_APPLIED:
            case DomainEventType::LIFT_NO_DRINK:
            case DomainEventType::STATE_CHANGE:
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
    Serial.print(F("  "));
    Serial.println(F(BOARD_BANNER_NAME));
    Serial.println(F("=================================================="));

    // 2. WS2812B 16-LED Ring Initialization
    led_ring.begin(PIN_LED_RING);
    led_ring.update(0, domain.get_status());
    Serial.print(F("[BOOT] WS2812B 16-LED Ring initialized on GPIO "));
    Serial.println(PIN_LED_RING);

    // 3. HX711 Weight Sensor Interface: PIN_HX711_DT (DT), PIN_HX711_SCK (SCK), 0.42 counts/g
    weight_sensor.begin(PIN_HX711_DT, PIN_HX711_SCK, CALIBRATION_SCALE_FACTOR);
    Serial.print(F("[BOOT] HX711 weight sensor initialized on GPIO "));
    Serial.print(PIN_HX711_DT);
    Serial.print(F("/"));
    Serial.println(PIN_HX711_SCK);

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

    // 4. Network Push Client & Wi-Fi Initialization
    network.begin(WIFI_SSID, WIFI_PASS, NTFY_TOPIC, NTFY_SERVER_URL);
    Serial.print(F("[BOOT] Wi-Fi client started for SSID: "));
    Serial.println(WIFI_SSID);

    // 5. Telemetry Console & Initial SYSTEM_BOOT Emission
    telemetry.begin();
    telemetry.setNetworkPush(&network);
    telemetry.setWifiStatus(network.getWifiStateStr());
    telemetry.setServerStatus(network.getServerReachabilityStr());

    // 6. Wi-Fi Captive Portal & WebServer Initialization
    wifi_portal.begin();

    const uint32_t boot_now_ms = millis();
    telemetry.emitTelemetry(Serial, domain.get_status(), "SYSTEM_BOOT", boot_now_ms);

    // Synchronize initial state baselines
    last_cup_state   = domain.get_status().cup_state;
    last_alert_state = domain.get_status().alert_state;

    // Synchronize scheduler baseline timestamps
    last_led_ms           = boot_now_ms;
    last_sensor_domain_ms = boot_now_ms;
    last_net_ms           = boot_now_ms;

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

    // Keep LedRing animation rates in sync with domain DEMO mode
    led_ring.setDemoMode(domain.is_demo_mode());

    // ------------------------------------------------------------------------
    // Task 2: 20 Hz (50 ms) - Weight Sensor Sampling & Domain Event Pump
    // ------------------------------------------------------------------------
    if (static_cast<uint32_t>(now_ms - last_sensor_domain_ms) >= INTERVAL_SENSOR_DOMAIN_MS) {
        last_sensor_domain_ms = now_ms;

        // Sample HX711 without blocking; fallback to cached weight if conversion pending
        float raw_weight_g = weight_sensor.readGrams(true);

        // Update core domain logic
        domain.update(raw_weight_g, now_ms);

        if (domain.consume_wifi_ap_trigger()) {
            telemetry.emitTelemetry(Serial, domain.get_status(), "WIFI_AP_TRIGGER", now_ms);
            network.startApFallback(now_ms);
            led_ring.setWifiApActive(true);
        }

        // Central Domain Event Pump: Drain and route all domain events
        dispatchDomainEvents(now_ms);

        // Catch any un-evented state transitions (e.g. CLI reset)
        checkStateTransitions(now_ms);
    }

    // ------------------------------------------------------------------------
    // Task 3: 50 Hz (20 ms) - WS2812B 16-LED Ring Animation & Status Engine
    // ------------------------------------------------------------------------
    if (static_cast<uint32_t>(now_ms - last_led_ms) >= INTERVAL_LED_RING_MS) {
        last_led_ms = now_ms;

        led_ring.update(now_ms, domain.get_status());
    }

    // ------------------------------------------------------------------------
    // Task 4: 20 Hz (50 ms) - Wi-Fi State Machine & Push Notification Queue
    // ------------------------------------------------------------------------
    if (static_cast<uint32_t>(now_ms - last_net_ms) >= INTERVAL_NETWORK_PUSH_MS) {
        last_net_ms = now_ms;

        network.update(now_ms);
        wifi_portal.update(now_ms);
        led_ring.setWifiApActive(network.isApActive());
        if (network.consumeWifiConnectedTrigger()) led_ring.triggerWifiConnected(now_ms);
        if (network.consumeWifiErrorTrigger()) led_ring.triggerWifiError(now_ms);
        telemetry.setWifiStatus(network.getWifiStateStr());
        telemetry.setServerStatus(network.getServerReachabilityStr());
    }

    // ------------------------------------------------------------------------
    // Task 5: Periodic Telemetry Heartbeat (5000 ms if no events emitted)
    // ------------------------------------------------------------------------
    telemetry.update(Serial, domain.get_status(), now_ms);

    // ------------------------------------------------------------------------
    // Task 6: Cooperative Yield to FreeRTOS IDLE Task (Feeds TWDT Watchdog)
    // ------------------------------------------------------------------------
    delay(1);
}
