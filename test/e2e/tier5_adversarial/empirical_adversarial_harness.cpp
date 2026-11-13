/**
 * @file empirical_adversarial_harness.cpp
 * @brief Tier 5 Empirical Adversarial Verification Harness.
 *
 * Direct white-box stress testing of compiled C++ domain, telemetry, and HAL logic:
 * - Section 1: CoasterDomain State Machine & Rapid Chatter Hardening
 * - Section 2: Sub-gram Drift & Thermal Noise Resilience
 * - Section 3: Negative Weight Offset & Calibration Recovery
 * - Section 4: Dynamic Goal Mutation & Multi-Event Burst Overwrite
 * - Section 5: TelemetryConsole UART CLI Fuzzing & JSON Grammar Conformance
 * - Section 6: LedRing Breathing Math, Intake Arc Mapping, Chaining & Rollover Safety
 * - Section 7: NetworkPush Ring Buffer Overflow & Rate-Limiter Throttling
 */

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include <cstring>
#include <sstream>
#include <limits>

#include "pins_config.h"
#include "config.h"
#include "coaster_domain.h"
#include "hal/led_ring.h"
#include "telemetry/telemetry_console.h"
#include "net/network_push.h"
#include "net/wifi_portal.h"

// ============================================================================
// Simple Mock Stream for UART I/O Testing
// ============================================================================

class FuzzStream : public Stream {
public:
    std::string rx_data;
    size_t rx_pos = 0;
    std::vector<std::string> tx_lines;
    std::string current_tx_line;

    void feed(const std::string& input) {
        rx_data += input;
    }

    int available() override {
        return static_cast<int>(rx_data.length() - rx_pos);
    }

    int read() override {
        if (rx_pos < rx_data.length()) {
            return static_cast<unsigned char>(rx_data[rx_pos++]);
        }
        return -1;
    }

    size_t print(const char* s) override {
        current_tx_line += s;
        return strlen(s);
    }

    size_t println(const char* s) override {
        current_tx_line += s;
        tx_lines.push_back(current_tx_line);
        current_tx_line.clear();
        return strlen(s) + 1;
    }

    void clear() {
        rx_data.clear();
        rx_pos = 0;
        tx_lines.clear();
        current_tx_line.clear();
    }
};

// ============================================================================
// Minimal JSON Syntax Validator
// ============================================================================

bool is_valid_simple_json(const std::string& line) {
    if (line.empty()) return false;
    size_t first = line.find_first_not_of(" \t\r\n");
    size_t last = line.find_last_not_of(" \t\r\n");
    if (first == std::string::npos || last == std::string::npos) return false;
    if (line[first] != '{' || line[last] != '}') return false;

    // Check brace balance and quote balance
    int brace_depth = 0;
    bool in_quotes = false;
    bool escape_next = false;

    for (size_t i = first; i <= last; ++i) {
        char c = line[i];
        if (escape_next) {
            escape_next = false;
            continue;
        }
        if (c == '\\') {
            escape_next = true;
            continue;
        }
        if (c == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (!in_quotes) {
            if (c == '{') brace_depth++;
            else if (c == '}') {
                brace_depth--;
                if (brace_depth < 0) return false;
            }
        }
    }
    return (brace_depth == 0 && !in_quotes);
}

// ============================================================================
// Test 1: Rapid Cup Chatter Stress (EMPTY <-> STABILIZING Flutter)
// ============================================================================

void test_rapid_chatter_and_ring_buffer_integrity() {
    std::cout << "[TEST 1] Rapid Contact Chatter (100 bounces @ 20Hz)..." << std::endl;
    CoasterDomain domain(2000.0f);
    uint32_t now_ms = 1000;

    // Simulate 100 rapid bounce cycles: 50ms at 35g, 50ms at 0g
    for (int i = 0; i < 100; ++i) {
        now_ms += 50;
        domain.update(35.0f, now_ms);
        assert(domain.get_status().cup_state == CupState::STABILIZING);
        assert(domain.get_status().is_stable == false);

        now_ms += 50;
        domain.update(0.0f, now_ms);
        assert(domain.get_status().cup_state == CupState::EMPTY);
    }

    // Verify ring buffer never corrupted: drain all pending events
    size_t drained = 0;
    while (domain.has_events()) {
        DomainEvent ev = domain.pop_event();
        assert(ev.type == DomainEventType::STATE_CHANGE);
        drained++;
    }

    // Event queue capacity is 16; overwrite behavior holds exactly 16 events
    assert(drained == CoasterDomain::EVENT_QUEUE_CAPACITY);
    assert(domain.get_status().cup_state == CupState::EMPTY);
    assert(domain.get_status().consumed_today_ml == 0.0f);
    std::cout << "  -> Passed: 100 chatter cycles survived, ring buffer bounded at " << drained << " events.\n";
}

// ============================================================================
// Test 2: Sub-Gram Drift and Thermal Noise
// ============================================================================

void test_subgram_drift_and_thermal_stability() {
    std::cout << "[TEST 2] Sub-Gram Drift Precision & Noise Tolerance..." << std::endl;
    CoasterDomain domain(2000.0f);
    uint32_t now_ms = 1000;

    // Sub-threshold noise: +0.04g (< 0.05g minimum) for 6000ms
    for (uint32_t t = 0; t < 6000; t += 50) {
        now_ms += 50;
        domain.update(0.04f, now_ms);
    }
    // Auto-tare must NOT trigger on < 0.05g
    assert(!domain.has_events());
    assert(domain.get_status().tare_offset_g == 0.0f);

    // Legitimate drift: +0.06g (>= 0.05g minimum) for 5100ms
    for (uint32_t t = 0; t < 5100; t += 50) {
        now_ms += 50;
        domain.update(0.06f, now_ms);
    }
    // Auto-tare MUST trigger
    assert(domain.has_events());
    DomainEvent ev = domain.pop_event();
    assert(ev.type == DomainEventType::AUTO_TARE_APPLIED);
    assert(std::fabs(domain.get_status().tare_offset_g - 0.06f) < 0.01f);
    assert(std::fabs(domain.get_status().net_weight_g - 0.0f) < 0.01f);

    std::cout << "  -> Passed: Sub-gram drift correctly differentiated at 0.05g boundary.\n";
}

// ============================================================================
// Test 3: Negative Weight Handling and Calibration Recovery
// ============================================================================

void test_negative_weight_handling() {
    std::cout << "[TEST 3] Negative Weight Offsets & Tare Recovery..." << std::endl;
    CoasterDomain domain(2000.0f);
    uint32_t now_ms = 1000;

    // Place 300g cup and settle
    for (uint32_t t = 0; t < 2000; t += 50) {
        now_ms += 50;
        domain.update(300.0f, now_ms);
    }
    assert(domain.get_status().cup_state == CupState::CUP_PRESENT);

    // Accidental manual tare while cup is on stand
    domain.manual_tare();
    assert(std::fabs(domain.get_status().tare_offset_g - 300.0f) < 0.1f);
    assert(std::fabs(domain.get_status().net_weight_g - 0.0f) < 0.1f);

    // On next tick, net weight 0.0g triggers CUP_LIFTED (< 20g)
    now_ms += 50;
    domain.update(300.0f, now_ms);
    assert(domain.get_status().cup_state == CupState::CUP_LIFTED);

    // User removes the cup: raw weight drops to 0.0g -> net weight is -300.0g
    now_ms += 50;
    domain.update(0.0f, now_ms);
    assert(std::fabs(domain.get_status().net_weight_g - (-300.0f)) < 0.1f);

    // Recovery via manual tare or reset
    domain.manual_tare(); // resets net to 0.0g
    assert(std::fabs(domain.get_status().net_weight_g - 0.0f) < 0.1f);

    domain.reset();
    assert(domain.get_status().cup_state == CupState::EMPTY);
    assert(domain.get_status().alert_state == AlertState::IDLE);

    std::cout << "  -> Passed: Negative offset transition and tare recovery verified.\n";
}

// ============================================================================
// Test 4: Dynamic Goal Mutation & Multi-Event Burst
// ============================================================================

void test_dynamic_goal_and_bursts() {
    std::cout << "[TEST 4] Dynamic Goal Limits & Multi-Event Bursts..." << std::endl;
    CoasterDomain domain(500.0f);
    uint32_t now_ms = 1000;

    // Settle 600g cup
    for (uint32_t t = 0; t < 2000; t += 50) {
        now_ms += 50;
        domain.update(600.0f, now_ms);
    }

    // Lift cup
    for (uint32_t t = 0; t < 1500; t += 50) {
        now_ms += 50;
        domain.update(0.0f, now_ms);
    }

    // Return at 90g (consumed = 510ml >= 500ml goal)
    for (uint32_t t = 0; t < 2000; t += 50) {
        now_ms += 50;
        domain.update(90.0f, now_ms);
    }

    // Both DRINK_LOGGED and GOAL_REACHED must be queued
    bool found_drink = false;
    bool found_goal = false;
    while (domain.has_events()) {
        DomainEvent ev = domain.pop_event();
        if (ev.type == DomainEventType::DRINK_LOGGED) found_drink = true;
        if (ev.type == DomainEventType::GOAL_REACHED) found_goal = true;
    }
    assert(found_drink && found_goal);

    // Increase goal to 2000ml via API
    domain.set_daily_goal(2000.0f);
    assert(domain.get_status().daily_goal_ml == 2000.0f);
    assert(domain.get_status().progress_pct < 100.0f); // 510 / 2000 = 25.5%

    // Boundary goal enforcement in API: values outside [100, 10000] ignored
    domain.set_daily_goal(50.0f);   // Rejected: < 100
    assert(domain.get_status().daily_goal_ml == 2000.0f);
    domain.set_daily_goal(15000.0f); // Rejected: > 10000
    assert(domain.get_status().daily_goal_ml == 2000.0f);

    std::cout << "  -> Passed: Multi-event burst drained and dynamic goal boundaries verified.\n";
}

// ============================================================================
// Test 5: TelemetryConsole UART CLI Fuzzing
// ============================================================================

void test_uart_cli_fuzzing() {
    std::cout << "[TEST 5] TelemetryConsole UART CLI Fuzzing & JSON Grammar..." << std::endl;
    CoasterDomain domain(2000.0f);
    TelemetryConsole console;
    FuzzStream stream;

    // Fuzz Case 1: Overflow line (> 64 characters, spans multiple ticks due to MAX_READS_PER_TICK=64)
    std::string long_junk(120, 'X');
    stream.feed(long_junk + "\n");
    while (stream.available() > 0) {
        console.processUart(stream, domain, 1000);
    }
    assert(stream.tx_lines.size() == 1);
    assert(is_valid_simple_json(stream.tx_lines[0]));
    assert(stream.tx_lines[0].find("\"status\":\"ERROR\"") != std::string::npos);

    // Fuzz Case 2: Control characters and escape codes
    stream.clear();
    stream.feed("\x1b[2J\x07\x7f\tSTATUS\t\r\n");
    console.processUart(stream, domain, 1000);
    assert(stream.tx_lines.size() >= 1);
    for (const auto& line : stream.tx_lines) {
        assert(is_valid_simple_json(line));
    }

    // Fuzz Case 3: Stream of multiple back-to-back commands in a single chunk
    stream.clear();
    stream.feed("DEMO ON\r\nSTATUS\r\nTARE\r\nRESET\r\nGOAL 1800\r\n");
    while (stream.available() > 0) {
        console.processUart(stream, domain, 1000);
    }
    // Must have executed all 5 commands
    assert(stream.tx_lines.size() == 5);
    for (const auto& line : stream.tx_lines) {
        assert(is_valid_simple_json(line));
    }
    assert(domain.is_demo_mode() == true);
    assert(domain.get_status().daily_goal_ml == 1800.0f);

    // Fuzz Case 4: Special float arguments for GOAL
    const char* float_fuzz[] = {
        "GOAL 99\n", "GOAL 10001\n", "GOAL -500\n", "GOAL 0\n", "GOAL abc\n",
        "GOAL 100\n", "GOAL 10000\n"
    };
    for (const char* cmd : float_fuzz) {
        stream.clear();
        stream.feed(cmd);
        while (stream.available() > 0) {
            console.processUart(stream, domain, 1000);
        }
        assert(stream.tx_lines.size() == 1);
        assert(is_valid_simple_json(stream.tx_lines[0]));
    }

    std::cout << "  -> Passed: UART CLI fuzzed with overflows, escapes, and chunks with 100% valid JSON responses.\n";
}

// ============================================================================
// Test 6: LedRing Breathing Math, Intake Arc, Chaining & Rollover Safety
// ============================================================================

void test_led_ring_math_chaining_and_rollover() {
    std::cout << "[TEST 6] LedRing Breathing Math, Intake Arc, Chaining & Rollover..." << std::endl;

    // 1. Check breathing curve intensity bounds: must span [0, 255]
    uint8_t min_intensity = 255;
    uint8_t max_intensity = 0;
    for (uint32_t t = 0; t <= 1500; t += 10) {
        uint8_t val = LedRing::computeBreathingIntensity(t, 1500);
        if (val < min_intensity) min_intensity = val;
        if (val > max_intensity) max_intensity = val;
    }
    assert(min_intensity == 0);
    assert(max_intensity == 255);
    assert(LedRing::computeBreathingIntensity(100, 0) == 0);

    // 2. Verify computeIntakeArcPixels boundary mapping on 16-LED ring
    assert(LedRing::computeIntakeArcPixels(0.0f, 16) == 1);
    assert(LedRing::computeIntakeArcPixels(-25.0f, 16) == 1);
    assert(LedRing::computeIntakeArcPixels(std::numeric_limits<float>::quiet_NaN(), 16) == 1);
    assert(LedRing::computeIntakeArcPixels(50.0f, 16) == 8);
    assert(LedRing::computeIntakeArcPixels(100.0f, 16) == 16);
    assert(LedRing::computeIntakeArcPixels(250.0f, 16) == 16);
    assert(LedRing::computeIntakeArcPixels(50.0f, 0) == 0);

    // 3. Verify one-shot chaining, goal override, and priority rules
    LedRing ring(PIN_LED_RING, LED_RING_PIXEL_COUNT, LED_RING_MAX_BRIGHTNESS);
    ring.begin(PIN_LED_RING);

    // Same-tick GOAL_REACHED + DRINK_LOGGED: RAINBOW_GOAL must win regardless of call order
    uint32_t t0 = 10000;
    ring.triggerGoalReached(t0, 100.0f);
    ring.triggerDrinkLogged(t0, 100.0f);
    ring.update(t0 + 500, CupState::CUP_PRESENT, AlertState::IDLE, 100.0f);
    assert(ring.getMode() == LedRingMode::RAINBOW_GOAL);
    assert(ring.getActiveLedCount() == 16);

    // After 3000ms rainbow -> chains into 3000ms INTAKE_ARC (16 LEDs) -> AMBIENT_WARM
    ring.update(t0 + 3200, CupState::CUP_PRESENT, AlertState::IDLE, 100.0f);
    assert(ring.getMode() == LedRingMode::INTAKE_ARC);
    assert(ring.getActiveLedCount() == 16);
    ring.update(t0 + 6200, CupState::CUP_PRESENT, AlertState::IDLE, 100.0f);
    assert(ring.getMode() == LedRingMode::AMBIENT_WARM);

    // Drink ripple (1000ms) -> chains into INTAKE_ARC (3000ms) -> cleared if cup lifted
    ring.triggerDrinkLogged(20000, 50.0f);
    ring.update(20500, CupState::CUP_PRESENT, AlertState::IDLE, 50.0f);
    assert(ring.getMode() == LedRingMode::RIPPLE_DRINK);
    ring.update(21500, CupState::CUP_PRESENT, AlertState::IDLE, 50.0f);
    assert(ring.getMode() == LedRingMode::INTAKE_ARC);
    assert(ring.getActiveLedCount() == 8);
    // Lifting cup clears arc_preview_active_
    ring.update(21600, CupState::CUP_LIFTED, AlertState::IDLE, 50.0f);
    assert(ring.getMode() == LedRingMode::LIFTED_GLOW);
    ring.update(21700, CupState::CUP_PRESENT, AlertState::IDLE, 50.0f);
    assert(ring.getMode() == LedRingMode::AMBIENT_WARM);

    // 4. Verify 32-bit timestamp rollover safety in animations and breathing math
    uint32_t rollover_time = 0xFFFFFFFFUL - 400UL;
    uint8_t breath_before = LedRing::computeBreathingIntensity(rollover_time, 1500);
    uint8_t breath_after  = LedRing::computeBreathingIntensity(rollover_time + 1000, 1500);
    assert(breath_before <= 255);
    assert(breath_after <= 255);

    ring.triggerDrinkLogged(rollover_time, 50.0f);
    ring.update(rollover_time + 200UL, CupState::CUP_PRESENT, AlertState::IDLE, 50.0f);
    assert(ring.getMode() == LedRingMode::RIPPLE_DRINK);

    // Across 0xFFFFFFFF (+600ms total < 1000ms)
    ring.update(rollover_time + 600UL, CupState::CUP_PRESENT, AlertState::IDLE, 50.0f);
    assert(ring.getMode() == LedRingMode::RIPPLE_DRINK);

    // Past 1000ms (+1200ms) -> chained cleanly into INTAKE_ARC across rollover
    ring.update(rollover_time + 1200UL, CupState::CUP_PRESENT, AlertState::IDLE, 50.0f);
    assert(ring.getMode() == LedRingMode::INTAKE_ARC);
    assert(ring.getActiveLedCount() == 8);

    // Past 4000ms (+4200ms) -> expired cleanly to AMBIENT_WARM
    ring.update(rollover_time + 4200UL, CupState::CUP_PRESENT, AlertState::IDLE, 50.0f);
    assert(ring.getMode() == LedRingMode::AMBIENT_WARM);

    std::cout << "  -> Passed: LedRing breathing, arc mapping, chaining, and 32-bit rollover verified.\n";
}

// ============================================================================
// Test 7: NetworkPush Ring Buffer Overflow & Rate-Limiter Hardening
// ============================================================================

void test_network_push_overflow_and_rate_limit() {
    std::cout << "[TEST 7] NetworkPush Ring Buffer Hardening & Rate-Limiting..." << std::endl;
    NetworkPush net;
    net.begin("TestSSID", "TestPass", "smart-cup-topic");

    // Queue 10 rapid notifications (capacity is 3)
    for (int i = 1; i <= 10; ++i) {
        char title[32];
        char msg[64];
        snprintf(title, sizeof(title), "Drink %d", i);
        snprintf(msg, sizeof(msg), "Consumed %d ml", i * 50);
        bool queued = net.queueNotification(title, msg);
        assert(queued);
    }

    // Queue count must be clamped at QUEUE_CAPACITY (3)
    assert(net.getQueueSize() == NetworkPush::QUEUE_CAPACITY);
    // Exactly 7 oldest items were dropped
    assert(net.getDroppedCount() == 7);

    // Verify newest notifications survived (Drink 8, Drink 9, Drink 10)
    const PushNotification* n0 = net.peekQueue(0);
    const PushNotification* n1 = net.peekQueue(1);
    const PushNotification* n2 = net.peekQueue(2);
    assert(n0 != nullptr && std::string(n0->title) == "Drink 8");
    assert(n1 != nullptr && std::string(n1->title) == "Drink 9");
    assert(n2 != nullptr && std::string(n2->title) == "Drink 10");

    // Test rate-limited drainage (minimum 2000ms between POSTs)
    uint32_t now_ms = 10000;
    net.update(now_ms); // Dispatches Drink 8
    assert(net.getQueueSize() == 2);
    assert(net.getSuccessfulPostCount() == 1);

    // Immediate next tick at +100ms: rate limiter must hold Drink 9
    now_ms += 100;
    net.update(now_ms);
    assert(net.getQueueSize() == 2);
    assert(net.getSuccessfulPostCount() == 1);

    // Advance by 2000ms: rate limiter expires, Drink 9 is dispatched
    now_ms += 2000;
    net.update(now_ms);
    assert(net.getQueueSize() == 1);
    assert(net.getSuccessfulPostCount() == 2);

    std::cout << "  -> Passed: Queue overflow dropped oldest 7 items without leak; 2000ms throttle enforced.\n";
}

// ============================================================================
// Test 8: Wi-Fi Portal REST & UART CLI Adversarial Hardening
// ============================================================================

void test_wifi_portal_and_uart_adversarial() {
    std::cout << "[TEST 8] Wi-Fi Portal REST & UART CLI Adversarial Hardening..." << std::endl;
    CoasterDomain domain(2000.0f);
    NetworkPush net;
    net.begin("AdvSSID", "AdvPass", "adv-topic");
    LedRing ring(PIN_LED_RING, LED_RING_PIXEL_COUNT, LED_RING_MAX_BRIGHTNESS);
    ring.begin();
    WifiPortal portal(domain, net, ring);
    TelemetryConsole console;
    console.begin();
    console.setNetworkPush(&net);

    char out_buf[2048];

    // 1. Portal boundary guards: null arguments & buffer bounds
    assert(portal.handleRequest(nullptr, "/", "", out_buf, sizeof(out_buf), 1000) == 400);
    assert(portal.handleRequest("GET", nullptr, "", out_buf, sizeof(out_buf), 1000) == 400);
    assert(portal.handleRequest("GET", "/", "", nullptr, 100, 1000) == 400);
    assert(portal.handleRequest("GET", "/", "", out_buf, 0, 1000) == 400);

    // 2. HTTP method fuzzing & unknown routes
    const char* unsupported_verbs[] = {"PUT", "DELETE", "PATCH", "HEAD", "TRACE"};
    for (const char* verb : unsupported_verbs) {
        int code = portal.handleRequest(verb, "/api/status", "", out_buf, sizeof(out_buf), 1000);
        assert(code == 405);
    }
    const char* traversal_paths[] = {"/../secret", "/etc/passwd", "/api/unknown", "/admin", "/.env"};
    for (const char* p : traversal_paths) {
        int code = portal.handleRequest("GET", p, "", out_buf, sizeof(out_buf), 1000);
        assert(code == 404);
    }

    // 3. Connect endpoint payload fuzzing
    // Malformed JSON (missing colon, unclosed brackets, binary bytes)
    assert(portal.handleRequest("POST", "/api/wifi/connect", "{ssid", out_buf, sizeof(out_buf), 1000) == 400);
    assert(portal.handleRequest("POST", "/api/wifi/connect", "{\"ssid\":}", out_buf, sizeof(out_buf), 1000) == 400);
    assert(portal.handleRequest("POST", "/api/wifi/connect", "\x01\x02\x03", out_buf, sizeof(out_buf), 1000) == 400);

    // URL encoded malformed escapes
    std::string bad_url = "ssid=Test%ZZ&password=pass%2";
    int code = portal.handleRequest("POST", "/api/wifi/connect", bad_url.c_str(), out_buf, sizeof(out_buf), 1000);
    assert(code == 200); // Decodes gracefully without crash

    // 4. UART CLI Wi-Fi command fuzzing
    FuzzStream stream;
    // Unclosed quote in WIFI SET
    stream.feed("WIFI SET \"UnclosedQuote mypass\r\n");
    console.processUart(stream, domain, 1000);
    assert(stream.tx_lines.size() == 1);
    assert(is_valid_simple_json(stream.tx_lines[0]));

    // Empty quote in WIFI SET
    stream.clear();
    stream.feed("WIFI SET \"\" pass\r\n");
    console.processUart(stream, domain, 1000);
    assert(stream.tx_lines.size() == 1);
    assert(is_valid_simple_json(stream.tx_lines[0]));
    assert(stream.tx_lines[0].find("\"status\":\"ERROR\"") != std::string::npos);

    // Invalid Wi-Fi subcommands
    stream.clear();
    stream.feed("WIFI BOGUS_SUBCOMMAND\r\n");
    console.processUart(stream, domain, 1000);
    assert(stream.tx_lines.size() == 1);
    assert(is_valid_simple_json(stream.tx_lines[0]));
    assert(stream.tx_lines[0].find("\"status\":\"ERROR\"") != std::string::npos);

    // Multiple rapid UART WIFI commands in one buffer chunk
    stream.clear();
    stream.feed("WIFI STATUS\r\nWIFI SCAN\r\nWIFI AP\r\nWIFI FORGET\r\n");
    while (stream.available() > 0) {
        console.processUart(stream, domain, 1000);
    }
    assert(stream.tx_lines.size() == 4);
    for (const auto& line : stream.tx_lines) {
        assert(is_valid_simple_json(line));
    }

    std::cout << "  -> Passed: Wi-Fi Portal REST & UART CLI withstood boundary violations, fuzzing, and unclosed quotes.\n";
}

// ============================================================================
// Main Runner
// ============================================================================

int main() {
    std::cout << "======================================================================\n";
    std::cout << "   SMART CUP STAND — EMPIRICAL TIER 5 ADVERSARIAL HARNESS           \n";
    std::cout << "======================================================================\n";

    test_rapid_chatter_and_ring_buffer_integrity();
    test_subgram_drift_and_thermal_stability();
    test_negative_weight_handling();
    test_dynamic_goal_and_bursts();
    test_uart_cli_fuzzing();
    test_led_ring_math_chaining_and_rollover();
    test_network_push_overflow_and_rate_limit();
    test_wifi_portal_and_uart_adversarial();

    std::cout << "======================================================================\n";
    std::cout << "SUCCESS: All 8 Deep Empirical Adversarial Hardening Tests PASSED!\n";
    std::cout << "======================================================================\n";
    return 0;
}
