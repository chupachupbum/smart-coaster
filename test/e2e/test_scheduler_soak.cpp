#include <iostream>
#include <cstdint>
#include <cassert>
#include <vector>
#include <cmath>

#include "coaster_domain.h"

int main() {
    std::cout << "======================================================\n";
    std::cout << "C++ Native CoasterDomain Rollover & Soak Verification\n";
    std::cout << "======================================================\n";

    CoasterDomain domain(2000.0f);

    // Multi-rate scheduler state
    uint32_t start_ms = 0xFFFFFFFF - 50000; // 50s before rollover
    uint32_t now_ms = start_ms;

    uint32_t last_sensor_domain_ms = now_ms;
    uint32_t last_led_ms = now_ms;
    uint32_t last_display_ms = now_ms;
    uint32_t last_net_ms = now_ms;

    uint32_t count_sensor = 0;
    uint32_t count_led = 0;
    uint32_t count_oled = 0;
    uint32_t count_net = 0;

    float current_weight_g = 0.0f;
    bool rollover_observed = false;

    // Simulate 100,000 FreeRTOS 1ms ticks across 0xFFFFFFFF
    for (uint32_t step = 0; step < 100000; ++step) {
        uint32_t prev_ms = now_ms;
        now_ms = (now_ms + 1); // 1ms tick

        if (now_ms < prev_ms) {
            rollover_observed = true;
        }

        // Scenario: Place cup at t = start + 5000ms
        if (step == 5000) {
            current_weight_g = 300.0f;
        }

        // 20Hz Sensor & Domain update
        if (static_cast<uint32_t>(now_ms - last_sensor_domain_ms) >= 50) {
            last_sensor_domain_ms = now_ms;
            domain.update(current_weight_g, now_ms);
            count_sensor++;
        }

        // 50Hz LED update
        if (static_cast<uint32_t>(now_ms - last_led_ms) >= 20) {
            last_led_ms = now_ms;
            count_led++;
        }

        // 10Hz OLED update
        if (static_cast<uint32_t>(now_ms - last_display_ms) >= 100) {
            last_display_ms = now_ms;
            count_oled++;
        }

        // 20Hz Network update
        if (static_cast<uint32_t>(now_ms - last_net_ms) >= 50) {
            last_net_ms = now_ms;
            count_net++;
        }
    }

    std::cout << "Simulation Completed: 100,000 steps\n";
    std::cout << "Rollover Observed    : " << (rollover_observed ? "YES" : "NO") << "\n";
    std::cout << "Task 2 (Sensor/Domain): " << count_sensor << " executions\n";
    std::cout << "Task 3 (Status LED)   : " << count_led << " executions\n";
    std::cout << "Task 4 (OLED Display) : " << count_oled << " executions\n";
    std::cout << "Task 5 (Network Push) : " << count_net << " executions\n";

    assert(rollover_observed);
    assert(count_sensor == 1999 || count_sensor == 2000);
    assert(count_led == 4999 || count_led == 5000);
    assert(count_oled == 999 || count_oled == 1000);
    assert(count_net == 1999 || count_net == 2000);

    // Verify domain state survived rollover cleanly
    const CoasterStatus& status = domain.get_status();
    assert(status.cup_state == CupState::CUP_PRESENT);
    assert(status.alert_state == AlertState::COUNTING);
    assert(status.is_stable == true);
    assert(std::abs(status.stable_weight_g - 300.0f) < 0.1f);

    std::cout << "[PASS] Domain state resting stably in CUP_PRESENT across rollover!\n";
    std::cout << "[PASS] Countdown active: " << status.countdown_s << " s remaining\n";

    // Simulate drink after rollover: lift cup for 2 seconds (40 domain ticks), then return with 250g
    for (uint32_t step = 0; step < 2000; ++step) {
        now_ms++;
        if (static_cast<uint32_t>(now_ms - last_sensor_domain_ms) >= 50) {
            last_sensor_domain_ms = now_ms;
            domain.update(0.0f, now_ms);
        }
    }
    assert(domain.get_status().cup_state == CupState::CUP_LIFTED);
    std::cout << "[PASS] Post-rollover cup lift correctly recognized\n";

    for (uint32_t step = 0; step < 2000; ++step) {
        now_ms++;
        if (static_cast<uint32_t>(now_ms - last_sensor_domain_ms) >= 50) {
            last_sensor_domain_ms = now_ms;
            domain.update(250.0f, now_ms);
        }
    }
    assert(domain.get_status().cup_state == CupState::CUP_PRESENT);
    assert(std::abs(domain.get_status().consumed_today_ml - 50.0f) < 0.1f);
    assert(domain.get_status().drink_count == 1);
    std::cout << "[PASS] Post-rollover drink logged: 50.0 ml, total: "
              << domain.get_status().consumed_today_ml << " ml\n";

    std::cout << "All C++ native scheduler & CoasterDomain tests PASSED cleanly!\n";
    return 0;
}
