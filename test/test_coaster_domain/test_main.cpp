#include <unity.h>
#include "coaster_domain.h"
#include <cstdlib>

static CoasterDomain* domain = nullptr;

void setUp(void) {
    domain = new CoasterDomain(2000.0f);
}

void tearDown(void) {
    delete domain;
    domain = nullptr;
}

// Helper: Feed steady weight for a duration in milliseconds
static void feed_weight(float weight_g, uint32_t start_ms, uint32_t duration_ms, uint32_t step_ms = 100) {
    for (uint32_t elapsed = 0; elapsed <= duration_ms; elapsed += step_ms) {
        domain->update(weight_g, start_ms + elapsed);
    }
}

// Helper: Place cup and wait for full stabilization (> 1500ms)
static void place_and_stabilize(float weight_g, uint32_t start_ms) {
    feed_weight(weight_g, start_ms, 2000, 100);
}

// Helper: Lift cup (weight drops below 20g threshold)
static void lift_cup(uint32_t start_ms) {
    feed_weight(0.0f, start_ms, 200, 50);
}

// Helper: Simulate tap gesture
static void simulate_tap(float weight_g, uint32_t start_ms, uint32_t duration_ms) {
    domain->update(weight_g, start_ms);
    if (duration_ms > 10) {
        domain->update(weight_g, start_ms + duration_ms / 2);
    }
    domain->update(0.0f, start_ms + duration_ms);
}

// ============================================================================
// 1. Initial State & Defaults
// ============================================================================

void test_initial_defaults(void) {
    const CoasterStatus& st = domain->get_status();
    TEST_ASSERT_EQUAL(CupState::EMPTY, st.cup_state);
    TEST_ASSERT_EQUAL(AlertState::IDLE, st.alert_state);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.raw_weight_g);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.tare_offset_g);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.net_weight_g);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.consumed_today_ml);
    TEST_ASSERT_EQUAL_FLOAT(2000.0f, st.daily_goal_ml);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.progress_pct);
    TEST_ASSERT_EQUAL_UINT32(0, st.drink_count);
    TEST_ASSERT_FALSE(st.alert_active);
    TEST_ASSERT_FALSE(st.demo_mode);
}

// ============================================================================
// 2. Auto-Tare Tests
// ============================================================================

void test_auto_tare_within_5g_for_5s_applies_offset(void) {
    uint32_t t = 1000;
    // Feed +3.0g for 4.9 seconds
    feed_weight(3.0f, t, 4900, 100);
    t += 4900;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().tare_offset_g);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, domain->get_status().net_weight_g);

    // Cross 5.0s threshold
    t += 200;
    domain->update(3.0f, t);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, domain->get_status().tare_offset_g);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().net_weight_g);

    // Verify AUTO_TARE event
    bool found_tare = false;
    while (domain->has_events()) {
        DomainEvent ev = domain->pop_event();
        if (ev.type == DomainEventType::AUTO_TARE_APPLIED) {
            found_tare = true;
            TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, ev.value1);
        }
    }
    TEST_ASSERT_TRUE(found_tare);
}

void test_auto_tare_negative_drift_applies_offset(void) {
    uint32_t t = 1000;
    feed_weight(-2.5f, t, 5200, 100);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -2.5f, domain->get_status().tare_offset_g);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, domain->get_status().net_weight_g);
}

void test_auto_tare_resets_if_weight_exceeds_5g(void) {
    uint32_t t = 1000;
    // Feed +4.0g for 3.0 seconds
    feed_weight(4.0f, t, 3000, 100);
    t += 3100;
    // Spike to +6.0g (exceeds 5.0g limit)
    domain->update(6.0f, t);
    t += 100;
    // Return to +4.0g for 3.0 seconds (total 6.2s elapsed, but continuous only 3.0s)
    feed_weight(4.0f, t, 3000, 100);
    t += 3000;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().tare_offset_g);

    // Advance remaining 2.1s (total 5.1s continuous after spike)
    feed_weight(4.0f, t, 2100, 100);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, domain->get_status().tare_offset_g);
}

void test_auto_tare_exact_boundary_5_00g_vs_5_05g(void) {
    // 1. Exact +5.00g triggers tare
    uint32_t t = 1000;
    feed_weight(5.00f, t, 5100, 100);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.00f, domain->get_status().tare_offset_g);

    // Reset domain for second boundary check
    delete domain;
    domain = new CoasterDomain(2000.0f);

    // 2. +5.05g exceeds boundary, must NOT tare after 6000ms
    t = 1000;
    feed_weight(5.05f, t, 6000, 100);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().tare_offset_g);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.05f, domain->get_status().net_weight_g);
}

void test_auto_tare_strictly_disabled_in_cup_present(void) {
    uint32_t t = 1000;
    place_and_stabilize(350.0f, t);
    t += 2100;
    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);

    // Sits on stand for 10.0 seconds
    feed_weight(350.1f, t, 10000, 100);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().tare_offset_g);
}

void test_auto_tare_demo_mode_accelerates_to_1s(void) {
    domain->set_demo_mode(true);
    uint32_t t = 1000;
    feed_weight(3.5f, t, 1100, 100);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, domain->get_status().tare_offset_g);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, domain->get_status().net_weight_g);
}

// ============================================================================
// 3. Stability & Cup Presence Tests
// ============================================================================

void test_cup_presence_transition_through_stabilizing(void) {
    uint32_t t = 1000;
    domain->update(0.0f, t);
    TEST_ASSERT_EQUAL(CupState::EMPTY, domain->get_status().cup_state);

    // Place 350g cup
    t += 100;
    domain->update(350.0f, t);
    TEST_ASSERT_EQUAL(CupState::STABILIZING, domain->get_status().cup_state);

    // Hold for 1.4s (window < 1.5s -> still STABILIZING)
    feed_weight(350.2f, t, 1400, 100);
    t += 1400;
    TEST_ASSERT_EQUAL(CupState::STABILIZING, domain->get_status().cup_state);
    TEST_ASSERT_FALSE(domain->get_status().is_stable);

    // Cross 1.5s -> CUP_PRESENT
    t += 200;
    domain->update(350.1f, t);
    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    TEST_ASSERT_EQUAL(AlertState::COUNTING, domain->get_status().alert_state);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 350.1f, domain->get_status().baseline_weight_g);
}

void test_stability_filter_rejects_unstable_span(void) {
    uint32_t t = 1000;
    // Alternating 350.0g and 354.0g (span 4.0g > 3.0g limit)
    for (int i = 0; i < 20; i++) {
        domain->update((i % 2 == 0) ? 350.0f : 354.0f, t);
        t += 100;
    }
    TEST_ASSERT_FALSE(domain->get_status().is_stable);
    TEST_ASSERT_EQUAL(CupState::STABILIZING, domain->get_status().cup_state);
}

void test_stability_span_exact_boundary_3_00g_vs_3_05g(void) {
    // 1. Span 3.00g is stable
    uint32_t t = 1000;
    for (int i = 0; i < 20; i++) {
        domain->update((i % 2 == 0) ? 350.0f : 353.00f, t);
        t += 100;
    }
    TEST_ASSERT_TRUE(domain->get_status().is_stable);
    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);

    // Reset domain for span 3.05g
    delete domain;
    domain = new CoasterDomain(2000.0f);
    t = 1000;
    for (int i = 0; i < 20; i++) {
        domain->update((i % 2 == 0) ? 350.0f : 353.05f, t);
        t += 100;
    }
    TEST_ASSERT_FALSE(domain->get_status().is_stable);
    TEST_ASSERT_EQUAL(CupState::STABILIZING, domain->get_status().cup_state);
}

void test_cup_presence_aborted_reverts_to_empty(void) {
    uint32_t t = 1000;
    domain->update(350.0f, t);
    TEST_ASSERT_EQUAL(CupState::STABILIZING, domain->get_status().cup_state);

    // Cup snatched off coaster before stabilizing
    t += 500;
    domain->update(0.0f, t);
    TEST_ASSERT_EQUAL(CupState::EMPTY, domain->get_status().cup_state);
}

void test_cup_lift_detected_below_20g(void) {
    uint32_t t = 1000;
    place_and_stabilize(400.0f, t);
    t += 2100;
    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);

    // Lift cup (weight drops below 20g)
    domain->update(12.0f, t);
    TEST_ASSERT_EQUAL(CupState::CUP_LIFTED, domain->get_status().cup_state);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 400.0f, domain->get_status().baseline_weight_g);
}

// ============================================================================
// 4. Drink Logging & Consumption Delta Tests
// ============================================================================

void test_drink_logged_when_returned_15g_lighter(void) {
    uint32_t t = 1000;
    place_and_stabilize(400.0f, t);
    t += 2100;

    // Lift cup
    lift_cup(t);
    t += 300;
    TEST_ASSERT_EQUAL(CupState::CUP_LIFTED, domain->get_status().cup_state);

    // Return cup 40g lighter (360.0g)
    place_and_stabilize(360.0f, t);
    t += 2100;

    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 40.0f, domain->get_status().consumed_today_ml);
    TEST_ASSERT_EQUAL_UINT32(1, domain->get_status().drink_count);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 360.0f, domain->get_status().baseline_weight_g);
}

void test_exact_15g_drink_boundary(void) {
    uint32_t t = 1000;
    place_and_stabilize(400.0f, t);
    t += 2100;

    lift_cup(t);
    t += 300;

    // Returned at exactly 385.0g (15.0g reduction)
    place_and_stabilize(385.0f, t);
    t += 2100;

    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 15.0f, domain->get_status().consumed_today_ml);
    TEST_ASSERT_EQUAL_UINT32(1, domain->get_status().drink_count);
}

void test_sub_threshold_drink_rejected_at_14_9g(void) {
    uint32_t t = 1000;
    place_and_stabilize(400.0f, t);
    t += 2100;

    lift_cup(t);
    t += 300;

    // Returned at 385.1g (14.9g reduction < 15.0g threshold)
    place_and_stabilize(385.1f, t);
    t += 2100;

    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().consumed_today_ml);
    TEST_ASSERT_EQUAL_UINT32(0, domain->get_status().drink_count);
}

// ============================================================================
// 5. Lift Without Drink Rejection Tests
// ============================================================================

void test_lift_without_drinking_does_not_increment_volume(void) {
    uint32_t t = 1000;
    place_and_stabilize(400.0f, t);
    t += 2100;

    lift_cup(t);
    t += 300;

    // Returned 8g lighter (392.0g)
    place_and_stabilize(392.0f, t);
    t += 2100;

    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().consumed_today_ml);
    TEST_ASSERT_EQUAL_UINT32(0, domain->get_status().drink_count);
}

void test_lift_without_drinking_does_not_reset_timer(void) {
    uint32_t t = 1000;
    place_and_stabilize(400.0f, t);
    t += 2100;

    // Advance 25 minutes (1500 seconds)
    t += 1500UL * 1000UL;
    domain->update(400.0f, t);
    uint32_t countdown_before = domain->get_status().countdown_s;
    TEST_ASSERT_TRUE(countdown_before <= 1205);

    // User lifts cup and returns 5g lighter
    lift_cup(t);
    t += 300;
    place_and_stabilize(395.0f, t);
    t += 2100;

    uint32_t countdown_after = domain->get_status().countdown_s;
    // Countdown MUST NOT reset back to 2700s (45m)
    TEST_ASSERT_TRUE(countdown_after < 1210);
    TEST_ASSERT_TRUE(std::abs((int32_t)countdown_before - (int32_t)countdown_after) <= 10);
}

void test_lift_without_drinking_updates_baseline(void) {
    uint32_t t = 1000;
    place_and_stabilize(400.0f, t);
    t += 2100;

    // First fidget: returns at 394g (-6g)
    lift_cup(t);
    t += 300;
    place_and_stabilize(394.0f, t);
    t += 2100;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 394.0f, domain->get_status().baseline_weight_g);

    // Second fidget: returns at 388g (-6g from 394g)
    lift_cup(t);
    t += 300;
    place_and_stabilize(388.0f, t);
    t += 2100;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 388.0f, domain->get_status().baseline_weight_g);

    // Intake remains 0ml
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().consumed_today_ml);
}

// ============================================================================
// 6. Refill Detection Tests
// ============================================================================

void test_refill_detected_on_30g_increase(void) {
    uint32_t t = 1000;
    place_and_stabilize(200.0f, t);
    t += 2100;

    lift_cup(t);
    t += 300;

    // Refill cup: return at 450.0g (+250g gain >= 30g)
    place_and_stabilize(450.0f, t);
    t += 2100;

    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().consumed_today_ml); // Refill is not drink
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 450.0f, domain->get_status().baseline_weight_g);

    bool found_refill = false;
    while (domain->has_events()) {
        DomainEvent ev = domain->pop_event();
        if (ev.type == DomainEventType::REFILL_DETECTED) {
            found_refill = true;
            TEST_ASSERT_FLOAT_WITHIN(0.5f, 250.0f, ev.value1);
        }
    }
    TEST_ASSERT_TRUE(found_refill);
}

void test_refill_exact_30g_boundary_vs_29_9g(void) {
    uint32_t t = 1000;
    place_and_stabilize(200.0f, t);
    t += 2100;

    // Exactly +30.0g gain (230.0g) -> Refill
    lift_cup(t);
    t += 300;
    place_and_stabilize(230.0f, t);
    t += 2100;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 230.0f, domain->get_status().baseline_weight_g);

    // Another lift and return +29.9g (259.9g) -> Lift no drink (neither drink nor refill)
    lift_cup(t);
    t += 300;
    place_and_stabilize(259.9f, t);
    t += 2100;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().consumed_today_ml);
}

// ============================================================================
// 7. Inactivity Reminder & Repeat Alert Tests
// ============================================================================

void test_reminder_alert_triggers_after_45_min_and_repeats(void) {
    uint32_t t = 1000;
    place_and_stabilize(400.0f, t);
    t += 2100;
    TEST_ASSERT_EQUAL(AlertState::COUNTING, domain->get_status().alert_state);

    // Advance 44 minutes
    t += 44UL * 60UL * 1000UL;
    domain->update(400.0f, t);
    TEST_ASSERT_EQUAL(AlertState::COUNTING, domain->get_status().alert_state);

    // Cross 45 minutes
    t += 1UL * 60UL * 1000UL + 1000;
    domain->update(400.0f, t);
    TEST_ASSERT_EQUAL(AlertState::ALERT_DUE, domain->get_status().alert_state);
    TEST_ASSERT_TRUE(domain->get_status().alert_active);
    TEST_ASSERT_EQUAL_UINT32(0, domain->get_status().countdown_s);

    // Advance 5 minutes in alert -> repeat alert
    t += 5UL * 60UL * 1000UL + 1000;
    domain->update(400.0f, t);
    TEST_ASSERT_EQUAL(AlertState::ALERT_REPEAT, domain->get_status().alert_state);
}

void test_valid_drink_clears_active_alert(void) {
    uint32_t t = 1000;
    place_and_stabilize(400.0f, t);
    t += 2100;

    // Fast-forward to alert due (46 minutes)
    t += 46UL * 60UL * 1000UL;
    domain->update(400.0f, t);
    TEST_ASSERT_EQUAL(AlertState::ALERT_DUE, domain->get_status().alert_state);
    TEST_ASSERT_TRUE(domain->get_status().alert_active);

    // Take drink: lift and return at 350g (50g drink)
    lift_cup(t);
    t += 300;
    place_and_stabilize(350.0f, t);
    t += 2100;

    TEST_ASSERT_EQUAL(AlertState::COUNTING, domain->get_status().alert_state);
    TEST_ASSERT_FALSE(domain->get_status().alert_active);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, domain->get_status().consumed_today_ml);
}

void test_lift_without_drink_does_not_clear_active_alert(void) {
    uint32_t t = 1000;
    place_and_stabilize(400.0f, t);
    t += 2100;

    t += 46UL * 60UL * 1000UL;
    domain->update(400.0f, t);
    TEST_ASSERT_EQUAL(AlertState::ALERT_DUE, domain->get_status().alert_state);

    // Fidget: return at 398g (only 2g drop < 15g)
    lift_cup(t);
    t += 300;
    place_and_stabilize(398.0f, t);
    t += 2100;

    TEST_ASSERT_EQUAL(AlertState::ALERT_DUE, domain->get_status().alert_state);
    TEST_ASSERT_TRUE(domain->get_status().alert_active);
}

void test_demo_mode_time_scaling(void) {
    domain->set_demo_mode(true);
    uint32_t t = 1000;
    place_and_stabilize(300.0f, t);
    t += 2100;
    TEST_ASSERT_EQUAL(AlertState::COUNTING, domain->get_status().alert_state);

    // Advance 46 seconds (exceeds 45s demo interval)
    t += 46UL * 1000UL;
    domain->update(300.0f, t);
    TEST_ASSERT_EQUAL(AlertState::ALERT_DUE, domain->get_status().alert_state);

    // Advance 6 seconds (exceeds 5s demo repeat interval)
    t += 6UL * 1000UL;
    domain->update(300.0f, t);
    TEST_ASSERT_EQUAL(AlertState::ALERT_REPEAT, domain->get_status().alert_state);
}

// ============================================================================
// 8. Control, Daily Goals & Robustness Tests
// ============================================================================

void test_manual_tare_and_reset_commands(void) {
    uint32_t t = 1000;
    domain->update(150.0f, t);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, domain->get_status().net_weight_g);

    domain->manual_tare();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, domain->get_status().tare_offset_g);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, domain->get_status().net_weight_g);

    // Place cup and drink 50g
    t += 500;
    place_and_stabilize(400.0f, t);
    t += 2100;
    lift_cup(t);
    t += 300;
    place_and_stabilize(350.0f, t);
    t += 2100;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, domain->get_status().consumed_today_ml);

    domain->reset();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().consumed_today_ml);
    TEST_ASSERT_EQUAL_UINT32(0, domain->get_status().drink_count);
}

void test_daily_goal_tracking_and_event(void) {
    domain->set_daily_goal(100.0f);
    uint32_t t = 1000;
    place_and_stabilize(500.0f, t);
    t += 2100;

    // Drink 120g (exceeds 100ml goal)
    lift_cup(t);
    t += 300;
    place_and_stabilize(380.0f, t);
    t += 2100;

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 120.0f, domain->get_status().consumed_today_ml);

    bool goal_event = false;
    while (domain->has_events()) {
        DomainEvent ev = domain->pop_event();
        if (ev.type == DomainEventType::GOAL_REACHED) {
            goal_event = true;
            TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, ev.value1);
            TEST_ASSERT_FLOAT_WITHIN(0.1f, 120.0f, ev.value2);
        }
    }
    TEST_ASSERT_TRUE(goal_event);
}

void test_timestamp_wraparound_safety(void) {
    // Start right before 32-bit unsigned rollover (0xFFFFFF00U)
    uint32_t t = 0xFFFFFF00U;
    place_and_stabilize(400.0f, t);
    // place_and_stabilize runs for 2000ms, so rollover (0xFFFFFF00U -> 0x000006D0U) already occurred
    uint32_t current_t = static_cast<uint32_t>(t + 2000U);
    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    TEST_ASSERT_EQUAL(AlertState::COUNTING, domain->get_status().alert_state);

    // Inactivity timer should calculate elapsed correctly using unsigned difference
    // Advance to 46 minutes after last drink
    uint32_t t_alert = current_t + static_cast<uint32_t>(46U * 60U * 1000U);
    domain->update(400.0f, t_alert);
    TEST_ASSERT_EQUAL(AlertState::ALERT_DUE, domain->get_status().alert_state);
}

void test_string_conversion_helpers(void) {
    TEST_ASSERT_EQUAL_STRING("EMPTY", CoasterDomain::cup_state_to_string(CupState::EMPTY));
    TEST_ASSERT_EQUAL_STRING("STABILIZING", CoasterDomain::cup_state_to_string(CupState::STABILIZING));
    TEST_ASSERT_EQUAL_STRING("CUP_PRESENT", CoasterDomain::cup_state_to_string(CupState::CUP_PRESENT));
    TEST_ASSERT_EQUAL_STRING("CUP_LIFTED", CoasterDomain::cup_state_to_string(CupState::CUP_LIFTED));
    TEST_ASSERT_EQUAL_STRING("RETURN_STABILIZING", CoasterDomain::cup_state_to_string(CupState::RETURN_STABILIZING));

    TEST_ASSERT_EQUAL_STRING("IDLE", CoasterDomain::alert_state_to_string(AlertState::IDLE));
    TEST_ASSERT_EQUAL_STRING("COUNTING", CoasterDomain::alert_state_to_string(AlertState::COUNTING));
    TEST_ASSERT_EQUAL_STRING("ALERT_DUE", CoasterDomain::alert_state_to_string(AlertState::ALERT_DUE));
    TEST_ASSERT_EQUAL_STRING("ALERT_REPEAT", CoasterDomain::alert_state_to_string(AlertState::ALERT_REPEAT));

    TEST_ASSERT_EQUAL_STRING("NONE", CoasterDomain::event_type_to_string(DomainEventType::NONE));
    TEST_ASSERT_EQUAL_STRING("STATE_CHANGE", CoasterDomain::event_type_to_string(DomainEventType::STATE_CHANGE));
    TEST_ASSERT_EQUAL_STRING("CUP_PLACED", CoasterDomain::event_type_to_string(DomainEventType::CUP_PLACED));
    TEST_ASSERT_EQUAL_STRING("CUP_LIFTED", CoasterDomain::event_type_to_string(DomainEventType::CUP_LIFTED));
    TEST_ASSERT_EQUAL_STRING("DRINK_LOGGED", CoasterDomain::event_type_to_string(DomainEventType::DRINK_LOGGED));
    TEST_ASSERT_EQUAL_STRING("LIFT_NO_DRINK", CoasterDomain::event_type_to_string(DomainEventType::LIFT_NO_DRINK));
    TEST_ASSERT_EQUAL_STRING("REFILL_DETECTED", CoasterDomain::event_type_to_string(DomainEventType::REFILL_DETECTED));
    TEST_ASSERT_EQUAL_STRING("REMINDER_ALERT", CoasterDomain::event_type_to_string(DomainEventType::REMINDER_ALERT));
    TEST_ASSERT_EQUAL_STRING("REPEAT_ALERT", CoasterDomain::event_type_to_string(DomainEventType::REPEAT_ALERT));
    TEST_ASSERT_EQUAL_STRING("AUTO_TARE", CoasterDomain::event_type_to_string(DomainEventType::AUTO_TARE_APPLIED));
    TEST_ASSERT_EQUAL_STRING("GOAL_REACHED", CoasterDomain::event_type_to_string(DomainEventType::GOAL_REACHED));
}

// ============================================================================
// 9. Jitter Robustness & Timing Invariants (Milestone 2 Iteration 1)
// ============================================================================

void test_stability_filter_with_52ms_non_harmonic_steps(void) {
    uint32_t t = 1000;
    domain->update(0.0f, t);
    TEST_ASSERT_EQUAL(CupState::EMPTY, domain->get_status().cup_state);

    // Feed 350.0g cup in 52ms increments (~19.23Hz, non-divisor of 1500ms)
    // Step 1 (t = 1052ms) triggers EMPTY -> STABILIZING and clears the stability buffer.
    // Step 2 (t = 1104ms) records the first retained sample in the buffer.
    // Steps 1 to 30: buffer duration reaches (30 - 2) * 52 = 1456ms (< 1500ms threshold)
    for (int i = 1; i <= 30; ++i) {
        t += 52;
        domain->update(350.0f, t);
        TEST_ASSERT_EQUAL(CupState::STABILIZING, domain->get_status().cup_state);
        TEST_ASSERT_FALSE(domain->get_status().is_stable);
    }

    // Step 31: t = 2612ms, buffer duration = 2612 - 1104 = 1508ms (>= 1500ms threshold -> CUP_PRESENT)
    t += 52;
    domain->update(350.0f, t);
    TEST_ASSERT_TRUE(domain->get_status().is_stable);
    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 350.0f, domain->get_status().baseline_weight_g);

    // Verify stability persists under continued 52ms sampling
    for (int i = 0; i < 15; ++i) {
        t += 52;
        domain->update(350.0f, t);
        TEST_ASSERT_TRUE(domain->get_status().is_stable);
        TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    }
}

void test_stability_filter_with_49ms_non_harmonic_steps(void) {
    uint32_t t = 1000;
    domain->update(0.0f, t);
    TEST_ASSERT_EQUAL(CupState::EMPTY, domain->get_status().cup_state);

    // Feed 350.0g cup in 49ms increments (~20.41Hz, non-divisor of 1500ms)
    // Step 1 (t = 1049ms) transitions to STABILIZING; Step 2 (t = 1098ms) is first retained sample.
    // Steps 1 to 32: buffer duration reaches (32 - 2) * 49 = 1470ms (< 1500ms threshold)
    for (int i = 1; i <= 32; ++i) {
        t += 49;
        domain->update(350.0f, t);
        TEST_ASSERT_EQUAL(CupState::STABILIZING, domain->get_status().cup_state);
        TEST_ASSERT_FALSE(domain->get_status().is_stable);
    }

    // Step 33: t = 2617ms, buffer duration = 2617 - 1098 = 1519ms (>= 1500ms threshold -> CUP_PRESENT)
    t += 49;
    domain->update(350.0f, t);
    TEST_ASSERT_TRUE(domain->get_status().is_stable);
    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 350.0f, domain->get_status().baseline_weight_g);
}

void test_stability_filter_with_simulated_scheduler_jitter(void) {
    uint32_t t = 1000;
    domain->update(0.0f, t);
    TEST_ASSERT_EQUAL(CupState::EMPTY, domain->get_status().cup_state);

    // Alternating 48ms and 53ms intervals simulating FreeRTOS task scheduling jitter
    // Step 1 (t = 1048ms) transitions to STABILIZING; Step 2 (t = 1101ms) is first retained sample.
    // Steps 1 to 31: buffer duration reaches 2563 - 1101 = 1462ms (< 1500ms threshold)
    bool toggle = false;
    for (int i = 1; i <= 31; ++i) {
        uint32_t step = toggle ? 53 : 48;
        t += step;
        toggle = !toggle;
        domain->update(350.0f, t);
        TEST_ASSERT_EQUAL(CupState::STABILIZING, domain->get_status().cup_state);
        TEST_ASSERT_FALSE(domain->get_status().is_stable);
    }

    // Step 32: step = 53ms, t = 2616ms, buffer duration = 2616 - 1101 = 1515ms (>= 1500ms -> CUP_PRESENT)
    uint32_t step = toggle ? 53 : 48;
    t += step;
    toggle = !toggle;
    domain->update(350.0f, t);
    TEST_ASSERT_TRUE(domain->get_status().is_stable);
    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);

    // Feed 30 more jittered samples to confirm jitter does not cause false destabilization
    for (int i = 0; i < 30; ++i) {
        uint32_t step_j = toggle ? 53 : 48;
        t += step_j;
        toggle = !toggle;
        domain->update(350.0f, t);
        TEST_ASSERT_TRUE(domain->get_status().is_stable);
        TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    }
}

void test_reset_while_cup_lifted_restores_empty_and_idle(void) {
    uint32_t t = 1000;
    // 1. Place cup and establish stable session
    place_and_stabilize(350.0f, t);
    t += 2100;
    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    TEST_ASSERT_EQUAL(AlertState::COUNTING, domain->get_status().alert_state);

    // 2. User lifts cup (< 20g net)
    domain->update(0.0f, t);
    TEST_ASSERT_EQUAL(CupState::CUP_LIFTED, domain->get_status().cup_state);

    // 3. User issues reset() while cup is removed from stand
    domain->reset();

    // 4. State must immediately revert to EMPTY and IDLE
    const CoasterStatus& st = domain->get_status();
    TEST_ASSERT_EQUAL(CupState::EMPTY, st.cup_state);
    TEST_ASSERT_EQUAL(AlertState::IDLE, st.alert_state);
    TEST_ASSERT_FALSE(st.alert_active);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.consumed_today_ml);
    TEST_ASSERT_EQUAL_UINT32(0, st.drink_count);

    // 5. Inactivity immunity: advance 46 minutes at 0.0g - no reminder should fire on empty stand
    t += 46UL * 60UL * 1000UL;
    domain->update(0.0f, t);
    TEST_ASSERT_EQUAL(CupState::EMPTY, domain->get_status().cup_state);
    TEST_ASSERT_EQUAL(AlertState::IDLE, domain->get_status().alert_state);
    TEST_ASSERT_FALSE(domain->get_status().alert_active);

    while (domain->has_events()) {
        DomainEvent ev = domain->pop_event();
        TEST_ASSERT_NOT_EQUAL(DomainEventType::REMINDER_ALERT, ev.type);
    }
}

void test_auto_tare_does_not_spam_at_zero(void) {
    uint32_t t = 1000;
    domain->update(0.0f, t);
    TEST_ASSERT_EQUAL(CupState::EMPTY, domain->get_status().cup_state);

    // 1. Drain any initial events
    while (domain->has_events()) domain->pop_event();

    // 2. Coaster sits idle at 0.0g for 30 seconds (6x normal auto-tare window)
    int auto_tare_events = 0;
    for (uint32_t el = 0; el <= 30000; el += 100) {
        domain->update(0.0f, t + el);
        while (domain->has_events()) {
            if (domain->pop_event().type == DomainEventType::AUTO_TARE_APPLIED) {
                auto_tare_events++;
            }
        }
    }
    // Must suppress spurious auto-tare when weight is already 0.0g
    TEST_ASSERT_EQUAL_INT(0, auto_tare_events);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().tare_offset_g);

    // 3. Realistic sensor drift: steady +2.0g for 6.0 seconds (> 5.0s window)
    t += 30100;
    for (uint32_t el = 0; el <= 6000; el += 100) {
        domain->update(2.0f, t + el);
        while (domain->has_events()) {
            if (domain->pop_event().type == DomainEventType::AUTO_TARE_APPLIED) {
                auto_tare_events++;
            }
        }
    }
    // Exactly one auto-tare event emitted to absorb the drift
    TEST_ASSERT_EQUAL_INT(1, auto_tare_events);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, domain->get_status().tare_offset_g);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, domain->get_status().net_weight_g);

    // 4. Continued idle at raw 2.0g (net 0.0g) for 15s must not spam additional events
    t += 6100;
    for (uint32_t el = 0; el <= 15000; el += 100) {
        domain->update(2.0f, t + el);
        while (domain->has_events()) {
            if (domain->pop_event().type == DomainEventType::AUTO_TARE_APPLIED) {
                auto_tare_events++;
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(1, auto_tare_events);
}

// ============================================================================
// 10. Load-Cell Triple-Tap Gesture Detector Tests
// ============================================================================

void test_triple_tap_gesture_triggers_ap_mode_once(void) {
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // Tap 1: 80g, 100ms duration (1000ms -> 1100ms)
    simulate_tap(80.0f, 1000, 100);
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // Tap 2: 75g, 150ms duration (1500ms -> 1650ms)
    simulate_tap(75.0f, 1500, 150);
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // Tap 3: 90g, 120ms duration (2000ms -> 2120ms)
    simulate_tap(90.0f, 2000, 120);

    // AP trigger must be pending and consumed exactly once
    TEST_ASSERT_TRUE(domain->consume_wifi_ap_trigger());
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // Event queue invariant: taps only emit STATE_CHANGE bounces, no unhandled events
    while (domain->has_events()) {
        DomainEvent ev = domain->pop_event();
        TEST_ASSERT_TRUE(ev.type == DomainEventType::STATE_CHANGE);
    }
}

void test_tap_gesture_rejections_and_window_timeout(void) {
    // 1. Light chatter: 35g (< 60g threshold)
    simulate_tap(35.0f, 1000, 100);
    simulate_tap(35.0f, 1300, 100);
    simulate_tap(35.0f, 1600, 100);
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // 2. Too-brief spike: 20ms (< 40ms threshold)
    simulate_tap(80.0f, 3000, 20);
    simulate_tap(80.0f, 3200, 20);
    simulate_tap(80.0f, 3400, 20);
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // 3. Too-slow hold: 700ms (> 600ms threshold)
    simulate_tap(80.0f, 5000, 700);
    simulate_tap(80.0f, 6000, 700);
    simulate_tap(80.0f, 7000, 700);
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // 4. Taps spaced > 4000ms apart (window timeout)
    simulate_tap(80.0f, 10000, 100); // Tap 1
    simulate_tap(80.0f, 12000, 100); // Tap 2
    // Tap 3 at 17000ms (> 4000ms from tap 1 and tap 2)
    simulate_tap(80.0f, 17000, 100); // Tap 3
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());
}

void test_tap_gesture_boundaries(void) {
    // Exact 60.0g, exact 40ms duration
    simulate_tap(60.0f, 1000, 40);
    simulate_tap(60.0f, 1200, 40);
    simulate_tap(60.0f, 1400, 40);
    TEST_ASSERT_TRUE(domain->consume_wifi_ap_trigger());
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // Exact 600ms duration at 60.0g
    simulate_tap(60.0f, 2000, 600);
    simulate_tap(60.0f, 2800, 600);
    simulate_tap(60.0f, 3600, 600);
    TEST_ASSERT_TRUE(domain->consume_wifi_ap_trigger());
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // Sub-threshold weight (59.9g < 60.0g)
    simulate_tap(59.9f, 5000, 100);
    simulate_tap(59.9f, 5200, 100);
    simulate_tap(59.9f, 5400, 100);
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());
}

void test_cup_placement_clears_partial_tap_count(void) {
    // 2 valid taps on empty stand
    simulate_tap(80.0f, 1000, 100);
    simulate_tap(80.0f, 1300, 100);
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // Place cup and stabilize into CUP_PRESENT
    place_and_stabilize(350.0f, 1600);
    TEST_ASSERT_EQUAL(CupState::CUP_PRESENT, domain->get_status().cup_state);
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // Cup removed and reset called while coaster is empty
    domain->update(0.0f, 4000);
    domain->reset();
    TEST_ASSERT_EQUAL(CupState::EMPTY, domain->get_status().cup_state);

    // A single 3rd tap at 4200ms must NOT trigger (since previous taps were cleared)
    simulate_tap(80.0f, 4200, 100);
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());

    // Completing 2 more taps in the new sequence WILL trigger AP mode
    simulate_tap(80.0f, 4500, 100);
    simulate_tap(80.0f, 4800, 100);
    TEST_ASSERT_TRUE(domain->consume_wifi_ap_trigger());
}

void test_reset_clears_tap_state(void) {
    simulate_tap(80.0f, 1000, 100);
    simulate_tap(80.0f, 1300, 100);
    domain->reset();
    simulate_tap(80.0f, 1600, 100);
    TEST_ASSERT_FALSE(domain->consume_wifi_ap_trigger());
}

// ============================================================================
// Unity Test Entry Point
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // 1. Defaults
    RUN_TEST(test_initial_defaults);

    // 2. Auto-Tare
    RUN_TEST(test_auto_tare_within_5g_for_5s_applies_offset);
    RUN_TEST(test_auto_tare_negative_drift_applies_offset);
    RUN_TEST(test_auto_tare_resets_if_weight_exceeds_5g);
    RUN_TEST(test_auto_tare_exact_boundary_5_00g_vs_5_05g);
    RUN_TEST(test_auto_tare_strictly_disabled_in_cup_present);
    RUN_TEST(test_auto_tare_demo_mode_accelerates_to_1s);

    // 3. Stability & Presence
    RUN_TEST(test_cup_presence_transition_through_stabilizing);
    RUN_TEST(test_stability_filter_rejects_unstable_span);
    RUN_TEST(test_stability_span_exact_boundary_3_00g_vs_3_05g);
    RUN_TEST(test_cup_presence_aborted_reverts_to_empty);
    RUN_TEST(test_cup_lift_detected_below_20g);

    // 4. Drink Logging
    RUN_TEST(test_drink_logged_when_returned_15g_lighter);
    RUN_TEST(test_exact_15g_drink_boundary);
    RUN_TEST(test_sub_threshold_drink_rejected_at_14_9g);

    // 5. Lift Rejection
    RUN_TEST(test_lift_without_drinking_does_not_increment_volume);
    RUN_TEST(test_lift_without_drinking_does_not_reset_timer);
    RUN_TEST(test_lift_without_drinking_updates_baseline);

    // 6. Refill Detection
    RUN_TEST(test_refill_detected_on_30g_increase);
    RUN_TEST(test_refill_exact_30g_boundary_vs_29_9g);

    // 7. Inactivity Reminders & Repeat Alerts
    RUN_TEST(test_reminder_alert_triggers_after_45_min_and_repeats);
    RUN_TEST(test_valid_drink_clears_active_alert);
    RUN_TEST(test_lift_without_drink_does_not_clear_active_alert);
    RUN_TEST(test_demo_mode_time_scaling);

    // 8. Control, Daily Goals & Robustness
    RUN_TEST(test_manual_tare_and_reset_commands);
    RUN_TEST(test_daily_goal_tracking_and_event);
    RUN_TEST(test_timestamp_wraparound_safety);
    RUN_TEST(test_string_conversion_helpers);

    // 9. Jitter Robustness & Timing Invariants
    RUN_TEST(test_stability_filter_with_52ms_non_harmonic_steps);
    RUN_TEST(test_stability_filter_with_49ms_non_harmonic_steps);
    RUN_TEST(test_stability_filter_with_simulated_scheduler_jitter);
    RUN_TEST(test_reset_while_cup_lifted_restores_empty_and_idle);
    RUN_TEST(test_auto_tare_does_not_spam_at_zero);

    // 10. Load-Cell Triple-Tap Gesture
    RUN_TEST(test_triple_tap_gesture_triggers_ap_mode_once);
    RUN_TEST(test_tap_gesture_rejections_and_window_timeout);
    RUN_TEST(test_tap_gesture_boundaries);
    RUN_TEST(test_cup_placement_clears_partial_tap_count);
    RUN_TEST(test_reset_clears_tap_state);

    return UNITY_END();
}
