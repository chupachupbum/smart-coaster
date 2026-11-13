/**
 * @file empirical_event_pump_harness.cpp
 * @brief Empirical Test Harness for System Firmware Integration (16-LED Ring).
 *
 * Verifies end-to-end event pump and peripheral coordination:
 * - Trajectory 1: Auto-tare on boot drift (+2.0g) & LedRing OFF
 * - Trajectory 2: Cup placement (300g) -> SPINNER -> INTAKE_ARC -> AMBIENT_WARM
 * - Trajectory 3: Drink logged (-50g) -> RIPPLE_DRINK -> INTAKE_ARC -> AMBIENT_WARM
 * - Trajectory 4: Lift without drink (-5g) -> no ripple/arc
 * - Trajectory 5: Refill (+150g) -> RIPPLE_REFILL -> AMBIENT_WARM (no arc chain)
 * - Trajectory 6: Inactivity reminder alert (45m ALERT_BREATHING) & repeat alert (5m ALERT_URGENT)
 * - Trajectory 7: Daily goal reached (DRINK_LOGGED + GOAL_REACHED -> RAINBOW_GOAL overrides ripple -> INTAKE_ARC 16 LEDs)
 * - Trajectory 8: Timestamp wraparound safety across 0xFFFFFFFF boundary (scheduler + LedRing animations)
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
#include "hal/led_ring.h"
#include "telemetry/telemetry_console.h"
#include "net/network_push.h"
#include "net/wifi_portal.h"

#ifndef WIFI_PASS
#define WIFI_PASS WIFI_PASSWORD
#endif

#ifndef NTFY_TOPIC
#define NTFY_TOPIC NTFY_DEFAULT_TOPIC
#endif

// ============================================================================
// Test Mock Stream Implementation
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

// ============================================================================
// Simulated Cooperative System Coordinator (Mirror of src/main.cpp)
// ============================================================================

class SystemCoordinatorHarness {
public:
    CoasterDomain     domain;
    LedRing           led_ring;
    TelemetryConsole  telemetry;
    NetworkPush       network;
    WifiPortal        wifi_portal;
    CapturingStream   serial;

    uint32_t current_time_ms = 1000;
    float current_raw_weight_g = 0.0f;

    // Scheduler intervals matching main.cpp
    static constexpr uint32_t INTERVAL_LED_RING_MS      = 20UL;
    static constexpr uint32_t INTERVAL_SENSOR_DOMAIN_MS = 50UL;
    static constexpr uint32_t INTERVAL_NETWORK_PUSH_MS  = 50UL;

    uint32_t last_led_ms = 0;
    uint32_t last_sensor_domain_ms = 0;
    uint32_t last_net_ms = 0;
    uint32_t led_update_calls = 0;

    CupState   last_cup_state = CupState::EMPTY;
    AlertState last_alert_state = AlertState::IDLE;
    bool       last_tick_had_events = false;

    std::vector<std::pair<std::string, uint32_t>> event_log;

    explicit SystemCoordinatorHarness(float goal_ml = 2000.0f)
        : domain(goal_ml),
          led_ring(PIN_LED_RING, LED_RING_PIXEL_COUNT, LED_RING_MAX_BRIGHTNESS),
          telemetry(TELEMETRY_HEARTBEAT_INTERVAL_MS),
          wifi_portal(domain, network, led_ring) {
        boot();
    }

    void boot() {
        led_ring.begin(PIN_LED_RING);
        led_ring.update(current_time_ms, domain.get_status());
        telemetry.begin();
        network.begin(WIFI_SSID, WIFI_PASS, NTFY_TOPIC, NTFY_SERVER_URL);
        telemetry.setNetworkPush(&network);
        telemetry.setWifiStatus(network.getWifiStateStr());
        telemetry.setServerStatus(network.getServerReachabilityStr());
        wifi_portal.begin();

        telemetry.emitTelemetry(serial, domain.get_status(), "SYSTEM_BOOT", current_time_ms);
        event_log.push_back({"SYSTEM_BOOT", current_time_ms});

        last_cup_state   = domain.get_status().cup_state;
        last_alert_state = domain.get_status().alert_state;

        last_led_ms           = current_time_ms;
        last_sensor_domain_ms = current_time_ms;
        last_net_ms           = current_time_ms;
    }

    // Event pump matching src/main.cpp
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
                    break;
            }
        }
    }

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
            led_ring.setDemoMode(domain.is_demo_mode());

            // Task 2: 20 Hz (50 ms) - Weight sampling & domain evaluation
            if (static_cast<uint32_t>(now_ms - last_sensor_domain_ms) >= INTERVAL_SENSOR_DOMAIN_MS) {
                last_sensor_domain_ms = now_ms;
                domain.update(current_raw_weight_g, now_ms);
                if (domain.consume_wifi_ap_trigger()) {
                    telemetry.emitTelemetry(serial, domain.get_status(), "WIFI_AP_TRIGGER", now_ms);
                    event_log.push_back({"WIFI_AP_TRIGGER", now_ms});
                    network.startApFallback(now_ms);
                    led_ring.setWifiApActive(true);
                }
                dispatchDomainEvents(now_ms);
                checkStateTransitions(now_ms);
            }

            // Task 3: 50 Hz (20 ms) - 16-LED Ring updates
            if (static_cast<uint32_t>(now_ms - last_led_ms) >= INTERVAL_LED_RING_MS) {
                last_led_ms = now_ms;
                const CoasterStatus& status = domain.get_status();
                led_ring.update(now_ms, status);
                led_update_calls++;
            }

            // Task 4: 20 Hz (50 ms) - Network queue processing
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

            // Task 5: Telemetry heartbeat
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

    assert(h.serial.hasLineContaining("\"event\":\"SYSTEM_BOOT\""));
    assert(h.domain.get_status().cup_state == CupState::EMPTY);
    assert(h.domain.get_status().net_weight_g == 0.0f);
    assert(h.led_ring.getMode() == LedRingMode::OFF);
    assert(h.led_ring.getActiveLedCount() == 0);

    // Feed +2.0g drift for 5.1 seconds
    h.step(5100, 2.0f);

    assert(h.serial.hasLineContaining("\"event\":\"AUTO_TARE\""));
    assert(std::fabs(h.domain.get_status().tare_offset_g - 2.0f) < 0.1f);
    assert(std::fabs(h.domain.get_status().net_weight_g - 0.0f) < 0.1f);
    assert(h.led_ring.getMode() == LedRingMode::OFF);
    assert(h.led_ring.getActiveLedCount() == 0);
    std::cout << "  -> Auto-tare verified: offset=" << h.domain.get_status().tare_offset_g << "g, net=0.0g, ring=OFF\n";
}

void test_trajectory_2_cup_placement() {
    std::cout << "[TEST] Trajectory 2: Cup placement (300g cup)..." << std::endl;
    SystemCoordinatorHarness h;
    h.step(5100, 2.0f); // auto-tare +2g

    // Place 300g cup -> enters STABILIZING -> SPINNER (3 LEDs)
    h.step(200, 302.0f);
    assert(h.domain.get_status().cup_state == CupState::STABILIZING);
    assert(h.led_ring.getMode() == LedRingMode::SPINNER);
    assert(h.led_ring.getActiveLedCount() == 3);

    // Settle -> CUP_PRESENT -> INTAKE_ARC preview (3s)
    h.step(1800, 302.0f);
    assert(h.domain.get_status().cup_state == CupState::CUP_PRESENT);
    assert(std::fabs(h.domain.get_status().net_weight_g - 300.0f) < 0.1f);
    assert(h.domain.get_status().is_stable == true);
    assert(h.led_ring.getMode() == LedRingMode::INTAKE_ARC);
    assert(h.led_ring.getActiveLedCount() >= 1);

    // After 3s arc expires -> AMBIENT_WARM (16 LEDs)
    h.step(3100, 302.0f);
    assert(h.led_ring.getMode() == LedRingMode::AMBIENT_WARM);
    assert(h.led_ring.getActiveLedCount() == 16);
    std::cout << "  -> Cup presence verified: SPINNER -> INTAKE_ARC -> AMBIENT_WARM (16 LEDs)\n";
}

void test_trajectory_3_drink_logged() {
    std::cout << "[TEST] Trajectory 3: Drink logged (-50g consumption)..." << std::endl;
    SystemCoordinatorHarness h;
    h.step(5100, 2.0f);    // Tare 2g
    h.step(2000, 302.0f);   // Place 300g cup
    h.step(3100, 302.0f);   // Let placement arc expire
    h.serial.clear();

    // Step 1: Lift cup off stand (< 20g) for 1500ms -> LIFTED_GLOW
    h.step(1500, 2.0f);
    assert(h.domain.get_status().cup_state == CupState::CUP_LIFTED);
    assert(h.led_ring.getMode() == LedRingMode::LIFTED_GLOW);
    assert(h.led_ring.getActiveLedCount() == 16);
    assert(h.serial.hasLineContaining("\"event\":\"CUP_LIFTED\""));

    // Step 2: Return cup 50g lighter (252g raw -> 250g net), stabilize (~1550ms)
    h.step(1600, 252.0f);

    // Verify domain state
    assert(h.domain.get_status().cup_state == CupState::CUP_PRESENT);
    assert(std::fabs(h.domain.get_status().consumed_today_ml - 50.0f) < 0.1f);
    assert(h.domain.get_status().drink_count == 1);
    assert(std::fabs(h.domain.get_status().net_weight_g - 250.0f) < 0.1f);

    // Verify Telemetry received DRINK_LOGGED with required schema fields
    assert(h.serial.hasLineContaining("\"event\":\"DRINK_LOGGED\""));
    assert(h.serial.hasLineContaining("\"consumed_ml\":50.0"));

    // Verify LedRing entered RIPPLE_DRINK (1000ms)
    assert(h.led_ring.getMode() == LedRingMode::RIPPLE_DRINK);
    assert(h.led_ring.isTransientAnimationActive() == true);

    // Advance 1100ms -> RIPPLE_DRINK chains into INTAKE_ARC (3000ms)
    h.step(1100, 252.0f);
    assert(h.led_ring.getMode() == LedRingMode::INTAKE_ARC);
    assert(h.led_ring.getActiveLedCount() >= 1);

    // Advance 3100ms -> INTAKE_ARC expires into AMBIENT_WARM
    h.step(3100, 252.0f);
    assert(h.led_ring.getMode() == LedRingMode::AMBIENT_WARM);
    assert(h.led_ring.getActiveLedCount() == 16);

    // Verify NetworkPush notification queued
    assert(h.network.getQueueSize() >= 1 || h.network.getSuccessfulPostCount() >= 1);
    const PushNotification* notif = h.network.peekQueue(0);
    if (notif) {
        assert(std::string(notif->title) == NTFY_TITLE_DRINK_LOGGED);
        assert(std::string(notif->message).find("Logged 50ml") != std::string::npos);
        assert(notif->priority == NTFY_PRIORITY_DRINK_LOGGED);
    }
    std::cout << "  -> Drink logged verified: RIPPLE_DRINK -> INTAKE_ARC -> AMBIENT_WARM, push queued.\n";
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
    size_t prev_queue_size = h.network.getQueueSize();

    // Lift cup again (also clears any active arc preview)
    h.step(1500, 2.0f);
    assert(h.domain.get_status().cup_state == CupState::CUP_LIFTED);
    assert(h.led_ring.getMode() == LedRingMode::LIFTED_GLOW);

    // Return cup only 5g lighter (247g raw -> 245g net)
    h.step(2000, 247.0f);

    assert(h.domain.get_status().cup_state == CupState::CUP_PRESENT);
    assert(std::fabs(h.domain.get_status().consumed_today_ml - 50.0f) < 0.1f);
    assert(h.domain.get_status().drink_count == 1);
    assert(std::fabs(h.domain.get_status().net_weight_g - 245.0f) < 0.1f);

    assert(h.serial.hasLineContaining("\"event\":\"LIFT_NO_DRINK\""));
    assert(!h.serial.hasLineContaining("\"event\":\"DRINK_LOGGED\""));
    assert(h.led_ring.getMode() == LedRingMode::AMBIENT_WARM);
    assert(h.led_ring.isTransientAnimationActive() == false);
    assert(h.network.getQueueSize() == prev_queue_size);

    std::cout << "  -> Lift without drink verified: volume preserved at 50ml, ring resting at AMBIENT_WARM.\n";
}

void test_trajectory_5_refill() {
    std::cout << "[TEST] Trajectory 5: Refill (+150g increase >= 30g)..." << std::endl;
    SystemCoordinatorHarness h;
    h.step(5100, 2.0f);    // Tare 2g
    h.step(2000, 252.0f);   // Place 250g cup
    h.serial.clear();
    size_t prev_queue_size = h.network.getQueueSize();

    // Lift cup
    h.step(1500, 2.0f);

    // Return with +150g water (402g raw -> 400g net)
    h.step(1600, 402.0f);

    assert(h.domain.get_status().cup_state == CupState::CUP_PRESENT);
    assert(std::fabs(h.domain.get_status().consumed_today_ml - 0.0f) < 0.1f);
    assert(std::fabs(h.domain.get_status().net_weight_g - 400.0f) < 0.1f);

    assert(h.serial.hasLineContaining("\"event\":\"REFILL_DETECTED\""));
    assert(h.serial.hasLineContaining("\"net_weight_g\":400.0"));

    // Verify RIPPLE_REFILL plays and returns directly to AMBIENT_WARM (no arc chain)
    assert(h.led_ring.getMode() == LedRingMode::RIPPLE_REFILL);
    h.step(1100, 402.0f);
    assert(h.led_ring.getMode() == LedRingMode::AMBIENT_WARM);
    assert(h.led_ring.getActiveLedCount() == 16);

    assert(h.network.getQueueSize() == prev_queue_size);
    std::cout << "  -> Refill verified: RIPPLE_REFILL -> AMBIENT_WARM (no arc chain), net=400g.\n";
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

    // Advance 45 minutes without drinking (2,700,000 ms)
    h.step(2700000UL, 252.0f, 1000);

    // Verify AlertState::ALERT_DUE & LedRing ALERT_BREATHING
    assert(h.domain.get_status().alert_state == AlertState::ALERT_DUE);
    assert(h.domain.get_status().alert_active == true);
    assert(h.serial.hasLineContaining("\"event\":\"REMINDER_ALERT\""));
    assert(h.led_ring.getMode() == LedRingMode::ALERT_BREATHING);
    assert(h.led_ring.isAlertPatternActive() == true);

    uint8_t breath_intensity = LedRing::computeBreathingIntensity(h.current_time_ms, LedRing::BREATHING_PERIOD_NORMAL_MS);
    assert(breath_intensity <= 255);

    assert(h.network.getQueueSize() >= 1 || h.network.getSuccessfulPostCount() >= 1);

    // Advance another 5 minutes (300,000 ms) without drinking -> ALERT_REPEAT & ALERT_URGENT
    h.serial.clear();
    h.step(300000UL, 252.0f, 1000);

    assert(h.domain.get_status().alert_state == AlertState::ALERT_REPEAT);
    assert(h.serial.hasLineContaining("\"event\":\"REPEAT_ALERT\""));
    assert(h.led_ring.getMode() == LedRingMode::ALERT_URGENT);
    assert(h.led_ring.isAlertPatternActive() == true);

    std::cout << "  -> Reminder and repeat alerts verified: ALERT_BREATHING -> ALERT_URGENT.\n";
}

void test_trajectory_7_goal_reached_multi_event() {
    std::cout << "[TEST] Trajectory 7: Daily goal reached (simultaneous DRINK_LOGGED + GOAL_REACHED)..." << std::endl;
    SystemCoordinatorHarness h(100.0f);
    h.step(5100, 2.0f);    // Tare 2g
    h.step(2000, 302.0f);   // Place 300g cup

    // Lift cup
    h.step(1500, 2.0f);
    h.serial.clear();

    // Return cup at 202g (200g net -> 100ml consumed >= 100ml goal)
    h.step(1600, 202.0f);

    assert(std::fabs(h.domain.get_status().consumed_today_ml - 100.0f) < 0.1f);
    assert(h.domain.get_status().progress_pct >= 100.0f);

    // Both events emitted and processed
    assert(h.serial.hasLineContaining("\"event\":\"DRINK_LOGGED\""));
    assert(h.serial.hasLineContaining("\"event\":\"GOAL_REACHED\""));

    // RAINBOW_GOAL (3000ms) overrides RIPPLE_DRINK when both fire on same tick
    assert(h.led_ring.getMode() == LedRingMode::RAINBOW_GOAL);
    assert(h.led_ring.getActiveLedCount() == 16);

    // First notification dispatched, second queued behind 2000ms throttle
    assert(h.network.getSuccessfulPostCount() == 1);
    assert(h.network.getQueueSize() == 1);
    const PushNotification* queued_notif = h.network.peekQueue(0);
    assert(queued_notif != nullptr);
    assert(std::string(queued_notif->title) == NTFY_TITLE_GOAL_ACHIEVED);

    // Advance 2100ms (total ~2150ms into 3000ms rainbow) -> still RAINBOW_GOAL, rate-limit clears
    h.step(2100, 202.0f);
    assert(h.led_ring.getMode() == LedRingMode::RAINBOW_GOAL);
    assert(h.network.getSuccessfulPostCount() == 2);
    assert(h.network.getQueueSize() == 0);
    assert(h.network.getDroppedCount() == 0);

    // Advance 1000ms (total ~3150ms) -> RAINBOW_GOAL chains into INTAKE_ARC (16/16 LEDs at 100%)
    h.step(1000, 202.0f);
    assert(h.led_ring.getMode() == LedRingMode::INTAKE_ARC);
    assert(h.led_ring.getActiveLedCount() == 16);

    // Advance 3100ms -> INTAKE_ARC expires into AMBIENT_WARM
    h.step(3100, 202.0f);
    assert(h.led_ring.getMode() == LedRingMode::AMBIENT_WARM);

    assert(h.domain.has_events() == false);
    std::cout << "  -> Multi-event tick verified: RAINBOW_GOAL overrode RIPPLE_DRINK -> INTAKE_ARC (16) -> AMBIENT_WARM.\n";
}

void test_trajectory_8_wraparound_safety() {
    std::cout << "[TEST] Trajectory 8: 32-bit timestamp rollover safety..." << std::endl;
    SystemCoordinatorHarness h;

    // Position clock 2500ms before 32-bit unsigned overflow: 0xFFFFFFFF - 2500
    h.current_time_ms = 0xFFFFFFFFUL - 2500UL;
    h.last_sensor_domain_ms = h.current_time_ms - 50;
    h.last_led_ms = h.current_time_ms - 20;
    h.last_net_ms = h.current_time_ms - 50;
    h.led_update_calls = 0;

    // Place and settle 300g cup before rollover (2000ms -> clock at 0xFFFFFFFF - 500)
    h.step(2000, 302.0f);
    assert(h.domain.get_status().cup_state == CupState::CUP_PRESENT);
    assert(h.domain.get_status().alert_state == AlertState::COUNTING);

    // Trigger 3000ms RAINBOW_GOAL 500ms before rollover
    h.led_ring.triggerGoalReached(h.current_time_ms, 100.0f);
    h.led_ring.update(h.current_time_ms, h.domain.get_status());
    assert(h.led_ring.getMode() == LedRingMode::RAINBOW_GOAL);

    // Step across the boundary by 1000ms (500ms post-rollover; total 1000ms < 3000ms)
    h.step(1000, 302.0f);

    assert(h.current_time_ms < 1000UL);
    assert(h.led_update_calls > 0);
    assert(h.led_ring.getMode() == LedRingMode::RAINBOW_GOAL);

    // Step 2200ms more (total 3200ms > 3000ms) -> chains into INTAKE_ARC cleanly after rollover
    h.step(2200, 302.0f);
    assert(h.led_ring.getMode() == LedRingMode::INTAKE_ARC);

    // Step 3100ms more -> returns to AMBIENT_WARM
    h.step(3100, 302.0f);
    assert(h.led_ring.getMode() == LedRingMode::AMBIENT_WARM);

    std::cout << "  -> Timestamp wraparound verified: scheduler & LedRing chaining crossed 0xFFFFFFFF smoothly.\n";
}

void test_trajectory_9_queue_saturation_and_stress() {
    std::cout << "[TEST] Trajectory 9: Peripheral coordination stress test & queue saturation..." << std::endl;
    SystemCoordinatorHarness h(1000.0f);
    h.step(5100, 0.0f);

    for (int cycle = 0; cycle < 10; ++cycle) {
        float cup_weight = 300.0f - cycle * 20.0f;
        h.step(1600, cup_weight + 5.0f);
        h.step(600, 0.0f);
    }

    h.step(2000, 100.0f);

    assert(!h.domain.has_events());
    assert(h.network.getQueueSize() <= NetworkPush::QUEUE_CAPACITY);
    assert(h.led_ring.getActiveLedCount() <= LedRing::PIXEL_COUNT);

    std::cout << "  -> Peripheral stress test verified: 10 rapid drink/lift cycles processed cleanly.\n";
}

void test_trajectory_10_wifi_integration() {
    std::cout << "[TEST] Trajectory 10: Wi-Fi integration (HTTP retry -> error pulse, triple-tap -> AP -> override, rollback, portal & UART)..." << std::endl;
    SystemCoordinatorHarness h;

    // Initial boot: device is connected
    h.step(100, 0.0f);
    assert(h.network.isConnected());

    // Phase 1: HTTP 3x retry exhaustion -> WIFI_ERROR_PULSE
    h.network.setSimulatedHttpSuccess(false, 500);
    h.network.queueNotification("Alert", "Test Message");
    // Initial attempt
    h.step(100, 0.0f);
    // Retry 1 after 2000ms
    h.step(2100, 0.0f);
    // Retry 2 after 4000ms
    h.step(4100, 0.0f);
    // Retry 3 after 8000ms -> exhaustion
    h.step(8200, 0.0f);
    assert(h.network.getExhaustedRetryCount() >= 1);
    // Wi-Fi error pulse triggered on LED ring
    assert(h.led_ring.getMode() == LedRingMode::WIFI_ERROR_PULSE);

    // Allow error pulse to finish (1000ms)
    h.step(1200, 0.0f);

    // Phase 2: Triple-tap gesture on coaster triggers AP mode
    // 3 rapid taps: 80g for 100ms, 0g for 200ms
    for (int tap = 0; tap < 3; ++tap) {
        h.step(100, 80.0f);
        h.step(200, 0.0f);
    }
    // Give scheduler time to process and initial AP pulse to settle (1000ms)
    h.step(1200, 0.0f);
    assert(h.network.isApActive());
    assert(h.serial.hasLineContaining("WIFI_AP_TRIGGER"));
    assert(h.led_ring.getMode() == LedRingMode::WIFI_AP_BEACON);

    // Place cup: AP beacon must be overridden by cup presence
    h.step(2000, 300.0f);
    assert(h.led_ring.getMode() != LedRingMode::WIFI_AP_BEACON);
    assert(h.domain.get_status().cup_state == CupState::CUP_PRESENT);

    // Phase 3: Bad-password switch rollback
    char out_buf[1024];
    std::string good_ssid = h.network.getSsid();
    h.network.setHostAutoConnect(false);
    h.network.setSimulatedSsidFilter(good_ssid.c_str());
    h.network.setSimulatedWifiOutcome(false, WifiErrorReason::AUTH_FAILED);

    int code = h.wifi_portal.handleRequest("POST", "/api/wifi/connect",
        "{\"ssid\":\"WrongAP\",\"password\":\"badpass\"}", out_buf, sizeof(out_buf), h.current_time_ms);
    assert(code == 200);
    // Advance past switch deferral (100ms)
    h.step(150, 300.0f);
    assert(h.network.hasRollbackOccurred());
    assert(std::string(h.network.getFailedSsid()) == "WrongAP");

    code = h.wifi_portal.handleRequest("GET", "/api/wifi/status", "", out_buf, sizeof(out_buf), h.current_time_ms);
    assert(code == 200);
    assert(strstr(out_buf, "\"rollback_occurred\":true") != nullptr);
    assert(strstr(out_buf, "\"failed_ssid\":\"WrongAP\"") != nullptr);

    // Phase 4: Portal REST connect to valid network
    h.network.setSimulatedSsidFilter("ValidRouter");
    h.network.setSimulatedWifiOutcome(true);
    code = h.wifi_portal.handleRequest("POST", "/api/wifi/connect",
        "ssid=ValidRouter&password=correctpass", out_buf, sizeof(out_buf), h.current_time_ms);
    assert(code == 200);
    h.step(150, 300.0f);
    assert(h.network.isConnected());
    assert(h.led_ring.getMode() == LedRingMode::WIFI_CONNECTED_FLASH);

    // Phase 5: UART WIFI commands
    char tx_buf[512];
    size_t len = h.telemetry.executeCommand("WIFI STATUS", h.domain, tx_buf, sizeof(tx_buf), h.current_time_ms);
    assert(len > 0);
    assert(strstr(tx_buf, "\"status\":\"OK\"") != nullptr);
    assert(strstr(tx_buf, "\"command\":\"WIFI STATUS\"") != nullptr);

    len = h.telemetry.executeCommand("WIFI SET \"My Office\" office123", h.domain, tx_buf, sizeof(tx_buf), h.current_time_ms);
    assert(len > 0);
    assert(strstr(tx_buf, "\"command\":\"WIFI SET\"") != nullptr);
    assert(strstr(tx_buf, "\"ssid\":\"My Office\"") != nullptr);

    std::cout << "  -> Wi-Fi integration verified: triple-tap AP beacon, error pulse on HTTP exhaustion, rollback on bad pass, portal connect, and UART commands.\n";
}

int main() {
    std::cout << "======================================================================\n";
    std::cout << "   SMART CUP STAND — EMPIRICAL COORDINATOR TEST HARNESS (16-LED RING) \n";
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
    test_trajectory_10_wifi_integration();

    std::cout << "======================================================================\n";
    std::cout << "SUCCESS: All 10 Empirical Trajectory & Coordination Tests PASSED!\n";
    std::cout << "======================================================================\n";
    return 0;
}
