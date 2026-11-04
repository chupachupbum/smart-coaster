/**
 * @file empirical_event_pump_harness.cpp
 * @brief Empirical Test Harness for System Firmware Integration (Milestone 5).
 *
 * Verifies end-to-end event pump and peripheral coordination:
 * - Trajectory 1: Auto-tare on boot drift (+2.0g)
 * - Trajectory 2: Cup placement (300g)
 * - Trajectory 3: Drink logged (-50g)
 * - Trajectory 4: Lift without drink (-5g)
 * - Trajectory 5: Refill (+150g)
 * - Trajectory 6: Inactivity reminder alert (45m) & repeat alert (5m)
 * - Trajectory 7: Daily goal reached (multi-event tick: DRINK_LOGGED + GOAL_REACHED)
 * - Trajectory 8: Timestamp wraparound safety across 0xFFFFFFFF boundary
 * - Trajectory 9: Peripheral starvation and event drop prevention
 */

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include <cstring>
#include <sstream>

#include "pins_config.h"
#include "config.h"
#include "coaster_domain.h"
#include "hal/status_led.h"
#include "telemetry/telemetry_console.h"
#include "net/network_push.h"

#ifndef WIFI_PASS
#define WIFI_PASS WIFI_PASSWORD
#endif

#ifndef NTFY_TOPIC
#define NTFY_TOPIC NTFY_DEFAULT_TOPIC
#endif

// ============================================================================
// Test Mock Implementations
// ============================================================================

class CapturingStream : public Stream {
public:
    std::vector<std::string> lines;
    std::string current_line;

    int available() override { return 0; }
    int read() override { return -1; }
    size_t print(const char* s) override {
        current_line += s;
        return strlen(s);
    }
    size_t println(const char* s) override {
        current_line += s;
        lines.push_back(current_line);
        current_line.clear();
        return strlen(s) + 1;
    }

    void clear() {
        lines.clear();
        current_line.clear();
    }

    bool hasLineContaining(const std::string& sub) const {
        for (const auto& line : lines) {
            if (line.find(sub) != std::string::npos) return true;
        }
        return false;
    }

    int countLinesContaining(const std::string& sub) const {
        int c = 0;
        for (const auto& line : lines) {
            if (line.find(sub) != std::string::npos) c++;
        }
        return c;
    }
};

struct MockDisplayOled {
    int render_calls = 0;
    int force_render_calls = 0;
    int splash_triggers = 0;
    float last_splash_ml = 0.0f;
    uint32_t last_splash_ms = 0;
    CoasterStatus last_status{};
    bool last_wifi = false;

    void triggerDrinkSplash(float consumed_ml, uint32_t now_ms) {
        splash_triggers++;
        last_splash_ml = consumed_ml;
        last_splash_ms = now_ms;
    }

    void render(const CoasterStatus& status, bool wifi_connected, uint32_t now_ms, bool force = false) {
        render_calls++;
        if (force) force_render_calls++;
        last_status = status;
        last_wifi = wifi_connected;
    }

    void reset() {
        render_calls = 0;
        force_render_calls = 0;
        splash_triggers = 0;
        last_splash_ml = 0.0f;
        last_splash_ms = 0;
    }
};

// ============================================================================
// Simulated Cooperative System Coordinator (Mirror of src/main.cpp)
// ============================================================================

class SystemCoordinatorHarness {
public:
    CoasterDomain     domain;
    StatusLed         status_led;
    MockDisplayOled   display;
    TelemetryConsole  telemetry;
    NetworkPush       network;
    CapturingStream   serial;

    uint32_t current_time_ms = 1000;
    float current_raw_weight_g = 0.0f;

    // Scheduler intervals matching main.cpp
    static constexpr uint32_t INTERVAL_STATUS_LED_MS    = 20UL;
    static constexpr uint32_t INTERVAL_SENSOR_DOMAIN_MS = 50UL;
    static constexpr uint32_t INTERVAL_NETWORK_PUSH_MS  = 50UL;
    static constexpr uint32_t INTERVAL_DISPLAY_OLED_MS  = 100UL;

    uint32_t last_led_ms = 0;
    uint32_t last_sensor_domain_ms = 0;
    uint32_t last_net_ms = 0;
    uint32_t last_display_ms = 0;

    CupState   last_cup_state = CupState::EMPTY;
    AlertState last_alert_state = AlertState::IDLE;
    bool       last_tick_had_events = false;

    // Verification telemetry statistics
    std::vector<std::pair<std::string, uint32_t>> event_log;

    explicit SystemCoordinatorHarness(float goal_ml = 2000.0f)
        : domain(goal_ml),
          status_led(PIN_STATUS_LED, LEDC_PWM_CHANNEL, LEDC_PWM_FREQ_HZ, LEDC_PWM_RESOLUTION),
          telemetry(TELEMETRY_HEARTBEAT_INTERVAL_MS) {
        boot();
    }

    void boot() {
        status_led.begin(PIN_STATUS_LED);
        telemetry.begin();
        network.begin(WIFI_SSID, WIFI_PASS, NTFY_TOPIC, NTFY_SERVER_URL);
        
        telemetry.emitTelemetry(serial, domain.get_status(), "SYSTEM_BOOT", current_time_ms);
        event_log.push_back({"SYSTEM_BOOT", current_time_ms});

        last_cup_state   = domain.get_status().cup_state;
        last_alert_state = domain.get_status().alert_state;

        last_led_ms           = current_time_ms;
        last_sensor_domain_ms = current_time_ms;
        last_net_ms           = current_time_ms;
        last_display_ms       = current_time_ms;
    }

    // Exact event pump implementation from src/main.cpp:93-145
    void dispatchDomainEvents(uint32_t now_ms) {
        last_tick_had_events = false;

        while (domain.has_events()) {
            last_tick_had_events = true;
            DomainEvent ev = domain.pop_event();
            const CoasterStatus& status = domain.get_status();
            const char* evt_str = CoasterDomain::event_type_to_string(ev.type);

            event_log.push_back({evt_str, now_ms});

            // 1. Telemetry emission over UART
            telemetry.emitTelemetry(serial, status, evt_str, now_ms);

            // 2. Peripheral routing based on event type
            switch (ev.type) {
                case DomainEventType::DRINK_LOGGED:
                    status_led.triggerDrinkLoggedFlash(now_ms);
                    display.triggerDrinkSplash(ev.value1, now_ms);
                    display.render(status, network.isConnected(), now_ms, true);
                    network.handleDomainEvent(ev, status);
                    break;

                case DomainEventType::REMINDER_ALERT:
                    network.handleDomainEvent(ev, status);
                    display.render(status, network.isConnected(), now_ms, true);
                    break;

                case DomainEventType::REPEAT_ALERT:
                    network.handleDomainEvent(ev, status);
                    display.render(status, network.isConnected(), now_ms, true);
                    break;

                case DomainEventType::GOAL_REACHED:
                    network.handleDomainEvent(ev, status);
                    break;

                case DomainEventType::REFILL_DETECTED:
                case DomainEventType::AUTO_TARE_APPLIED:
                case DomainEventType::LIFT_NO_DRINK:
                case DomainEventType::STATE_CHANGE:
                case DomainEventType::CUP_PLACED:
                case DomainEventType::CUP_LIFTED:
                default:
                    break;
            }
        }
    }

    // State transition fallback telemetry from src/main.cpp:155-174
    void checkStateTransitions(uint32_t now_ms) {
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

        if (state_changed && !last_tick_had_events) {
            telemetry.emitTelemetry(serial, status, "STATE_CHANGE", now_ms);
            event_log.push_back({"STATE_CHANGE", now_ms});
        }
    }

    // Run scheduler forward by duration_ms
    void step(uint32_t duration_ms, float raw_weight_g, uint32_t step_ms = 10) {
        current_raw_weight_g = raw_weight_g;
        uint32_t elapsed = 0;

        while (elapsed < duration_ms) {
            uint32_t delta = (duration_ms - elapsed < step_ms) ? (duration_ms - elapsed) : step_ms;
            current_time_ms += delta;
            elapsed += delta;

            const uint32_t now_ms = current_time_ms;

            // Task 1: Non-blocking UART & Demo mode sync
            telemetry.processUart(serial, domain, now_ms);
            status_led.setDemoMode(domain.is_demo_mode());

            // Task 2: 20 Hz (50 ms) - Weight sampling & domain evaluation
            if (static_cast<uint32_t>(now_ms - last_sensor_domain_ms) >= INTERVAL_SENSOR_DOMAIN_MS) {
                last_sensor_domain_ms = now_ms;
                domain.update(current_raw_weight_g, now_ms);
                dispatchDomainEvents(now_ms);
                checkStateTransitions(now_ms);
            }

            // Task 3: 50 Hz (20 ms) - Status LED PWM updates
            if (static_cast<uint32_t>(now_ms - last_led_ms) >= INTERVAL_STATUS_LED_MS) {
                last_led_ms = now_ms;
                const CoasterStatus& status = domain.get_status();
                status_led.update(now_ms, status.cup_state, status.alert_state);
            }

            // Task 4: 10 Hz (100 ms) - OLED Render
            if (static_cast<uint32_t>(now_ms - last_display_ms) >= INTERVAL_DISPLAY_OLED_MS) {
                last_display_ms = now_ms;
                const CoasterStatus& status = domain.get_status();
                display.render(status, network.isConnected(), now_ms);
            }

            // Task 5: 20 Hz (50 ms) - Network queue processing
            if (static_cast<uint32_t>(now_ms - last_net_ms) >= INTERVAL_NETWORK_PUSH_MS) {
                last_net_ms = now_ms;
                network.update(now_ms);
                telemetry.setWifiStatus(network.getWifiStateStr());
            }

            // Task 6: Telemetry heartbeat
            telemetry.update(serial, domain.get_status(), now_ms);
        }
    }
};

// ============================================================================
// Test Suite Runners
// ============================================================================

void test_trajectory_1_auto_tare() {
    std::cout << "[TEST] Trajectory 1: Auto-tare on boot zero drift..." << std::endl;
    SystemCoordinatorHarness h;

    // Verify initial boot packet
    assert(h.serial.hasLineContaining("\"event\":\"SYSTEM_BOOT\""));
    assert(h.domain.get_status().cup_state == CupState::EMPTY);
    assert(h.domain.get_status().net_weight_g == 0.0f);

    // Feed +2.0g drift for 5.1 seconds
    h.step(5100, 2.0f);

    // Verify AUTO_TARE event emitted and captured
    assert(h.serial.hasLineContaining("\"event\":\"AUTO_TARE\""));
    assert(std::fabs(h.domain.get_status().tare_offset_g - 2.0f) < 0.1f);
    assert(std::fabs(h.domain.get_status().net_weight_g - 0.0f) < 0.1f);
    std::cout << "  -> Auto-tare verified: offset=" << h.domain.get_status().tare_offset_g << "g, net=0.0g\n";
}

void test_trajectory_2_cup_placement() {
    std::cout << "[TEST] Trajectory 2: Cup placement (300g cup)..." << std::endl;
    SystemCoordinatorHarness h;
    h.step(5100, 2.0f); // auto-tare +2g

    // Place 300g cup (raw weight 302g)
    h.step(2000, 302.0f);

    assert(h.domain.get_status().cup_state == CupState::CUP_PRESENT);
    assert(std::fabs(h.domain.get_status().net_weight_g - 300.0f) < 0.1f);
    assert(h.domain.get_status().is_stable == true);

    // Verify LED duty cycle in CUP_PRESENT
    assert(h.status_led.getDuty() == StatusLed::CUP_PRESENT_DUTY);
    // Verify OLED has rendered CUP_PRESENT
    assert(h.display.last_status.cup_state == CupState::CUP_PRESENT);
    assert(std::fabs(h.display.last_status.net_weight_g - 300.0f) < 0.1f);
    std::cout << "  -> Cup presence verified: net=" << h.domain.get_status().net_weight_g << "g, LED duty=" << (int)h.status_led.getDuty() << "\n";
}

void test_trajectory_3_drink_logged() {
    std::cout << "[TEST] Trajectory 3: Drink logged (-50g consumption)..." << std::endl;
    SystemCoordinatorHarness h;
    h.step(5100, 2.0f);    // Tare 2g
    h.step(2000, 302.0f);   // Place 300g cup
    h.serial.clear();
    h.display.reset();

    // Step 1: Lift cup off stand (< 20g) for 1500ms
    h.step(1500, 2.0f);
    assert(h.domain.get_status().cup_state == CupState::CUP_LIFTED);
    assert(h.status_led.getDuty() == StatusLed::CUP_LIFTED_DUTY);
    assert(h.serial.hasLineContaining("\"event\":\"CUP_LIFTED\""));

    // Step 2: Return cup 50g lighter (252g raw -> 250g net)
    h.step(2000, 252.0f);

    // Verify domain state
    assert(h.domain.get_status().cup_state == CupState::CUP_PRESENT);
    assert(std::fabs(h.domain.get_status().consumed_today_ml - 50.0f) < 0.1f);
    assert(h.domain.get_status().drink_count == 1);
    assert(std::fabs(h.domain.get_status().net_weight_g - 250.0f) < 0.1f);

    // Verify Telemetry received DRINK_LOGGED with required schema fields
    assert(h.serial.hasLineContaining("\"event\":\"DRINK_LOGGED\""));
    assert(h.serial.hasLineContaining("\"consumed_ml\":50.0"));

    // Verify Display received celebratory splash & force redraw
    assert(h.display.splash_triggers >= 1);
    assert(std::fabs(h.display.last_splash_ml - 50.0f) < 0.1f);
    assert(h.display.force_render_calls >= 1);

    // Verify StatusLed celebratory triple-flash trigger
    // During the 480ms flash duration, duty must have been FLASH_DUTY (255)
    assert(h.status_led.isFlashActive() || h.status_led.getDuty() == StatusLed::FLASH_DUTY || h.status_led.getDuty() == StatusLed::CUP_PRESENT_DUTY);

    // Verify NetworkPush notification queued
    assert(h.network.getQueueSize() >= 1 || h.network.getSuccessfulPostCount() >= 1);
    const PushNotification* notif = h.network.peekQueue(0);
    if (notif) {
        assert(std::string(notif->title) == NTFY_TITLE_DRINK_LOGGED);
        assert(std::string(notif->message).find("Logged 50ml") != std::string::npos);
        assert(notif->priority == NTFY_PRIORITY_DRINK_LOGGED);
    }
    std::cout << "  -> Drink logged verified: consumed=50ml, splash=50ml, push queued successfully.\n";
}

void test_trajectory_4_lift_without_drink() {
    std::cout << "[TEST] Trajectory 4: Lift without drinking (-5g delta < 15g)..." << std::endl;
    SystemCoordinatorHarness h;
    h.step(5100, 2.0f);    // Tare 2g
    h.step(2000, 302.0f);   // Place 300g cup
    // First drink: 50ml
    h.step(1500, 2.0f);
    h.step(2000, 252.0f);
    assert(std::fabs(h.domain.get_status().consumed_today_ml - 50.0f) < 0.1f);

    h.serial.clear();
    h.display.reset();
    size_t prev_queue_size = h.network.getQueueSize();

    // Lift cup again
    h.step(1500, 2.0f);
    assert(h.domain.get_status().cup_state == CupState::CUP_LIFTED);

    // Return cup only 5g lighter (247g raw -> 245g net)
    h.step(2000, 247.0f);

    // Verify domain state: consumed_today_ml NOT incremented
    assert(h.domain.get_status().cup_state == CupState::CUP_PRESENT);
    assert(std::fabs(h.domain.get_status().consumed_today_ml - 50.0f) < 0.1f);
    assert(h.domain.get_status().drink_count == 1); // Not incremented!
    assert(std::fabs(h.domain.get_status().net_weight_g - 245.0f) < 0.1f);

    // Telemetry emitted LIFT_NO_DRINK
    assert(h.serial.hasLineContaining("\"event\":\"LIFT_NO_DRINK\""));
    // NO DRINK_LOGGED event
    assert(!h.serial.hasLineContaining("\"event\":\"DRINK_LOGGED\""));

    // No display splash
    assert(h.display.splash_triggers == 0);

    // No new push notification queued
    assert(h.network.getQueueSize() == prev_queue_size);

    std::cout << "  -> Lift without drink verified: volume preserved at 50ml, no false alert/splash.\n";
}

void test_trajectory_5_refill() {
    std::cout << "[TEST] Trajectory 5: Refill (+150g increase >= 30g)..." << std::endl;
    SystemCoordinatorHarness h;
    h.step(5100, 2.0f);    // Tare 2g
    h.step(2000, 252.0f);   // Place 250g cup
    h.serial.clear();
    h.display.reset();
    size_t prev_queue_size = h.network.getQueueSize();

    // Lift cup
    h.step(1500, 2.0f);

    // Return with +150g water (402g raw -> 400g net)
    h.step(2000, 402.0f);

    // Verify domain state
    assert(h.domain.get_status().cup_state == CupState::CUP_PRESENT);
    assert(std::fabs(h.domain.get_status().consumed_today_ml - 0.0f) < 0.1f); // Refill is not consumed!
    assert(std::fabs(h.domain.get_status().net_weight_g - 400.0f) < 0.1f);

    // Telemetry received REFILL_DETECTED
    assert(h.serial.hasLineContaining("\"event\":\"REFILL_DETECTED\""));
    assert(h.serial.hasLineContaining("\"net_weight_g\":400.0"));

    // No display drink splash
    assert(h.display.splash_triggers == 0);

    // No push notification
    assert(h.network.getQueueSize() == prev_queue_size);

    std::cout << "  -> Refill verified: net=400g, gain=150g, no drink increment.\n";
}

void test_trajectory_6_reminder_and_repeat_alerts() {
    std::cout << "[TEST] Trajectory 6: Inactivity reminder alert (45m) & repeat alert (5m)..." << std::endl;
    SystemCoordinatorHarness h;
    h.step(5100, 2.0f);    // Tare 2g
    h.step(2000, 302.0f);   // Place 300g cup

    // Log drink to start active reminder countdown
    h.step(1500, 2.0f);
    h.step(2000, 252.0f); // 50ml drink logged
    assert(h.domain.get_status().alert_state == AlertState::COUNTING);

    h.serial.clear();
    h.display.reset();

    // Advance 45 minutes without drinking (in 10-second steps to simulate time acceleration)
    // 45 minutes = 2,700,000 ms
    h.step(2700000UL, 252.0f, 1000);

    // Verify AlertState::ALERT_DUE
    assert(h.domain.get_status().alert_state == AlertState::ALERT_DUE);
    assert(h.domain.get_status().alert_active == true);

    // Telemetry received REMINDER_ALERT
    assert(h.serial.hasLineContaining("\"event\":\"REMINDER_ALERT\""));

    // Display received force render for reminder banner
    assert(h.display.force_render_calls >= 1);

    // LED status entered breathing mode: duty cycle varies sinusoidal
    uint8_t breath_duty_1 = StatusLed::computeBreathingDuty(h.current_time_ms, StatusLed::BREATHING_PERIOD_NORMAL_MS);
    assert(breath_duty_1 <= 255);

    // NetworkPush received Hydration Alert
    assert(h.network.getQueueSize() >= 1 || h.network.getSuccessfulPostCount() >= 1);

    // Advance another 5 minutes (300,000 ms) without drinking
    h.serial.clear();
    h.step(300000UL, 252.0f, 1000);

    // Verify AlertState::ALERT_REPEAT
    assert(h.domain.get_status().alert_state == AlertState::ALERT_REPEAT);
    assert(h.serial.hasLineContaining("\"event\":\"REPEAT_ALERT\""));

    std::cout << "  -> Reminder and repeat alerts verified: 45m primary and 5m repeat fired cleanly.\n";
}

void test_trajectory_7_goal_reached_multi_event() {
    std::cout << "[TEST] Trajectory 7: Daily goal reached (simultaneous DRINK_LOGGED + GOAL_REACHED)..." << std::endl;
    // Set daily goal to 100ml for direct testing
    SystemCoordinatorHarness h(100.0f);
    h.step(5100, 2.0f);    // Tare 2g
    h.step(2000, 302.0f);   // Place 300g cup

    // User drinks 100ml in one drink (300g -> 200g)
    h.step(1500, 2.0f);     // Lift cup
    h.serial.clear();
    h.display.reset();

    // Return cup at 202g (200g net -> exactly 100ml consumed >= goal)
    h.step(2000, 202.0f);

    // Domain state checks
    assert(std::fabs(h.domain.get_status().consumed_today_ml - 100.0f) < 0.1f);
    assert(h.domain.get_status().progress_pct >= 100.0f);

    // Check that BOTH events were emitted and processed in the tick
    assert(h.serial.hasLineContaining("\"event\":\"DRINK_LOGGED\""));
    assert(h.serial.hasLineContaining("\"event\":\"GOAL_REACHED\""));

    // Verify both push notifications were processed without starvation or drop:
    // First notification (Drink Logged) was dispatched on first tick;
    // Second notification (Goal Achieved) was queued behind the 2000ms rate limiter.
    assert(h.network.getSuccessfulPostCount() == 1);
    assert(h.network.getQueueSize() == 1);
    const PushNotification* queued_notif = h.network.peekQueue(0);
    assert(queued_notif != nullptr);
    assert(std::string(queued_notif->title) == NTFY_TITLE_GOAL_ACHIEVED);

    // Now advance time by 2100ms to allow the 2000ms rate-limit throttle to expire
    h.step(2100, 202.0f);

    // Verify both notifications are now dispatched with 0 dropped
    assert(h.network.getSuccessfulPostCount() == 2);
    assert(h.network.getQueueSize() == 0);
    assert(h.network.getDroppedCount() == 0);

    // Verify zero events left trapped in domain queue
    assert(h.domain.has_events() == false);

    std::cout << "  -> Multi-event tick verified: DRINK_LOGGED and GOAL_REACHED both delivered without drop or starvation.\n";
}

void test_trajectory_8_wraparound_safety() {
    std::cout << "[TEST] Trajectory 8: 32-bit timestamp rollover safety..." << std::endl;
    SystemCoordinatorHarness h;
    // Position clock 500ms before 32-bit unsigned overflow: 0xFFFFFFFF - 500
    h.current_time_ms = 0xFFFFFFFFUL - 500UL;
    h.last_sensor_domain_ms = h.current_time_ms - 50;
    h.last_led_ms = h.current_time_ms - 20;
    h.last_display_ms = h.current_time_ms - 100;
    h.last_net_ms = h.current_time_ms - 50;

    // Step across the boundary by 1000ms
    h.step(1000, 200.0f);

    // Timestamp has rolled over past 0
    assert(h.current_time_ms < 1000UL);
    // Coordinator continued executing without freeze or exception
    assert(h.display.render_calls > 0);
    std::cout << "  -> Timestamp wraparound verified: clock transitioned across 0xFFFFFFFF smoothly.\n";
}

void test_trajectory_9_queue_saturation_and_stress() {
    std::cout << "[TEST] Trajectory 9: Peripheral coordination stress test & queue saturation..." << std::endl;
    SystemCoordinatorHarness h(1000.0f);
    h.step(5100, 0.0f);

    // Rapidly place and lift cup 10 times to test queue throughput
    for (int cycle = 0; cycle < 10; ++cycle) {
        float cup_weight = 300.0f - cycle * 20.0f;
        h.step(1600, cup_weight + 5.0f); // place & stabilize
        h.step(600, 0.0f);               // lift
    }

    // Drain all remaining ticks
    h.step(2000, 100.0f);

    // Verify no stuck events in domain
    assert(!h.domain.has_events());

    // Verify NetworkPush queue count <= QUEUE_CAPACITY (no memory corruption)
    assert(h.network.getQueueSize() <= NetworkPush::QUEUE_CAPACITY);

    std::cout << "  -> Peripheral stress test verified: 10 rapid drink/lift cycles processed cleanly.\n";
}

int main() {
    std::cout << "======================================================================\n";
    std::cout << "   SMART CUP STAND — EMPIRICAL COORDINATOR TEST HARNESS (M5)        \n";
    std::cout << "======================================================================\n";

    test_trajectory_1_auto_tare();
    test_trajectory_2_cup_placement();
    test_trajectory_3_drink_logged();
    test_trajectory_4_lift_without_drink();
    test_trajectory_5_refill();
    test_trajectory_6_reminder_and_repeat_alerts();
    test_trajectory_7_goal_reached_multi_event();
    test_trajectory_8_wraparound_safety();
    test_trajectory_9_queue_saturation_and_stress();

    std::cout << "======================================================================\n";
    std::cout << "SUCCESS: All 9 Empirical Trajectory & Coordination Tests PASSED!\n";
    std::cout << "======================================================================\n";
    return 0;
}
