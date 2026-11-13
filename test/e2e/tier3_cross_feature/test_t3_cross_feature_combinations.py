"""Tier 3: Cross-Feature Combinations & Pairwise Interaction Tests."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_event,
    assert_no_event,
    assert_consumed_ml,
    assert_weight_g,
    assert_notification_sent,
)


class TestT3CrossFeatureCombinations:
    """Cross-feature integration and pairwise behavioral verification."""

    def test_auto_tare_then_placement_and_refill(self):
        """Pairwise: Auto-tare on empty plate followed by cup placement and subsequent refill."""
        harness = E2ETestHarness()

        # Step 1: Empty coaster experiences +3.0g scale drift and auto-tares after 5.1s
        harness.tick(duration_ms=5100, step_ms=50, weight_g=3.0)
        assert_event(harness.events, "AUTO_TARE", {"tare_offset": 3.0})
        assert_weight_g(harness.telemetry, 0.0)

        # Step 2: User places 250g mug (raw 253.0g) -> net weight is 250.0g
        harness.place_cup(weight_g=253.0, duration_ms=2000)
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_weight_g(harness.telemetry, 250.0)

        # Step 3: User lifts mug and adds 200g water (raw 453.0g -> net 450.0g)
        harness.lift_cup(duration_ms=1000)
        harness.return_cup(weight_g=453.0, duration_ms=2000)

        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_event(harness.events, "REFILL_DETECTED", {"refill_delta_ml": 200.0})
        assert_consumed_ml(harness.telemetry, 0.0)

    def test_lift_without_drink_followed_by_reminder_timeout(self):
        """Pairwise: Lift-no-drink fidget does not perturb 45m reminder deadline."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # 30 minutes pass
        harness.advance_time(duration_ms=1800 * 1000, step_ms=5000)

        # User fidgets: picks up cup, holds for 2s, returns 5g lighter
        harness.lift_cup(duration_ms=2000)
        harness.return_cup(weight_g=395.0, duration_ms=2000)
        assert_event(harness.events, "LIFT_NO_DRINK")

        # Advance remaining 15 minutes + 5 seconds (905 seconds)
        harness.advance_time(duration_ms=905 * 1000, step_ms=5000)

        # Reminder MUST trigger because fidget did not reset the timer!
        assert harness.telemetry["alert"] is True
        assert_event(harness.events, "REMINDER_ALERT")

    def test_active_alert_with_refill_preserves_alert(self):
        """Pairwise: Refilling during active reminder alert does NOT clear the alert."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=200.0, duration_ms=2000)

        # Inactivity triggers reminder alert (45m)
        harness.advance_time(duration_ms=2705 * 1000, step_ms=5000)
        assert harness.telemetry["alert"] is True
        assert_event(harness.events, "REMINDER_ALERT")

        # User notices cup is low, refills +250g water (does not drink yet!)
        harness.refill_cup(refill_ml=250.0, duration_ms=2000)
        assert_event(harness.events, "REFILL_DETECTED")

        # Invariant: Adding water does not hydrate user; alert MUST remain active!
        assert harness.telemetry["alert"] is True
        assert harness.telemetry["countdown_s"] == 0

    def test_active_alert_with_valid_drink_clears_alert(self):
        """Pairwise: Valid drink during active alert clears alert and resets countdown."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Trigger alert
        harness.advance_time(duration_ms=2705 * 1000, step_ms=5000)
        assert harness.telemetry["alert"] is True

        # User drinks 120ml
        harness.take_drink(sip_ml=120.0, duration_ms=2000)

        assert harness.telemetry["alert"] is False
        assert_consumed_ml(harness.telemetry, 120.0)
        assert 2690 <= harness.telemetry["countdown_s"] <= 2700

    def test_rapid_lifts_with_concurrent_status_queries(self):
        """Pairwise: Concurrent serial STATUS queries during rapid lifting cycles."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=350.0, duration_ms=2000)

        # 4 fast lift-and-replace cycles with query after each
        for i in range(4):
            harness.lift_cup(duration_ms=300)
            resp_lifted = harness.send_command("STATUS")
            assert resp_lifted["state"] == "CUP_LIFTED"

            harness.return_cup(weight_g=350.0, duration_ms=2000)
            resp_present = harness.send_command("STATUS")
            assert resp_present["state"] == "CUP_PRESENT"

    def test_demo_mode_switch_during_active_countdown(self):
        """Pairwise: Enabling DEMO mode scales countdown time dynamically."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # 10 minutes pass in 1x normal mode
        harness.advance_time(duration_ms=600 * 1000, step_ms=5000)
        assert harness.telemetry["demo"] is False

        # Switch to DEMO mode (60x time acceleration)
        harness.send_command("DEMO ON")
        assert harness.telemetry["demo"] is True

        # Advance 46 seconds (exceeds 45s demo reminder interval)
        harness.advance_time(duration_ms=46 * 1000, step_ms=500)
        assert harness.telemetry["alert"] is True
        assert_event(harness.events, "REMINDER_ALERT")

        # Switch back to DEMO OFF
        harness.send_command("DEMO OFF")
        assert harness.telemetry["demo"] is False

    def test_manual_tare_issued_while_cup_is_lifted(self):
        """Pairwise: Manual TARE while cup is lifted zeroes offset for empty coaster."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=350.0, duration_ms=2000)

        # User lifts cup to drink
        harness.lift_cup(duration_ms=1000)
        assert_state(harness.telemetry, "CUP_LIFTED")

        # While lifted, user issues TARE command
        resp = harness.send_command("TARE")
        assert resp["status"] == "OK"
        assert_weight_g(harness.telemetry, 0.0)

        # Return cup at 300g (50ml sip)
        harness.return_cup(weight_g=300.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 50.0)

    def test_dynamic_goal_update_right_before_goal_drink(self):
        """Pairwise: Updating GOAL right before a drink triggers GOAL_REACHED notification."""
        harness = E2ETestHarness(daily_goal_ml=2000.0)
        harness.place_cup(weight_g=1000.0, duration_ms=2000)

        # Drink 1: 500ml -> consumed 500ml
        harness.take_drink(sip_ml=500.0, duration_ms=2000)
        harness.refill_cup(refill_ml=500.0, duration_ms=2000)

        # User dynamically lowers goal to 600ml
        resp_goal = harness.send_command("GOAL 600")
        assert resp_goal["status"] == "OK"
        assert resp_goal["goal_ml"] == 600

        # User takes 150ml drink -> brings total consumed to 650ml (>600ml goal!)
        harness.take_drink(sip_ml=150.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 650.0)
        assert_event(harness.events, "GOAL_REACHED", {"goal_ml": 600.0})
        assert_notification_sent(harness.notifications, "Daily Goal Achieved!")

    def test_accidental_vibration_bump_during_stabilizing(self):
        """Pairwise: Mechanical vibration bump delays stabilization until settled."""
        harness = E2ETestHarness()
        # Initial placement at 350g
        harness.tick(duration_ms=500, step_ms=50, weight_g=350.0)
        assert_state(harness.telemetry, "STABILIZING")

        # Heavy bump: weight spikes to 370g then oscillates for 1000ms
        for _ in range(10):
            harness.tick(duration_ms=50, step_ms=50, weight_g=345.0)
            harness.tick(duration_ms=50, step_ms=50, weight_g=368.0)

        # Still stabilizing because span is 23g
        assert_state(harness.telemetry, "STABILIZING")

        # Now settles down for 1600ms
        harness.tick(duration_ms=1600, step_ms=50, weight_g=350.0)
        assert_state(harness.telemetry, "CUP_PRESENT")

    def test_refill_immediately_followed_by_sip(self):
        """Pairwise: Refill followed immediately by drink calculates drink from new baseline."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=200.0, duration_ms=2000)

        # Step 1: Refill +250g -> cup weight is now 450g
        harness.refill_cup(refill_ml=250.0, duration_ms=2000)
        assert_event(harness.events, "REFILL_DETECTED", {"refill_delta_ml": 250.0})
        assert_consumed_ml(harness.telemetry, 0.0)

        # Step 2: Immediately take 75ml sip -> cup weight is now 375g (75g lighter than 450g)
        harness.take_drink(sip_ml=75.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 75.0)
        assert_event(harness.events, "DRINK_LOGGED", {"drink_delta_ml": 75.0})

    def test_led_ring_state_synchronization(self):
        """Pairwise: 16-LED ring state machine tracks lifecycle transitions, chaining, and alerts."""
        harness = E2ETestHarness()
        # 1. EMPTY state
        assert_state(harness.telemetry, "EMPTY")
        assert harness.led["mode"] == "OFF"
        assert harness.led["active_leds"] == 0

        # 2. STABILIZING state
        harness.tick(duration_ms=200, step_ms=50, weight_g=300.0)
        assert_state(harness.telemetry, "STABILIZING")
        assert harness.led["mode"] == "SPINNER"
        assert harness.led["active_leds"] == 3

        # 3. CUP_PRESENT (first 3s after placement -> INTAKE_ARC)
        harness.tick(duration_ms=1600, step_ms=50, weight_g=300.0)
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert harness.led["mode"] == "INTAKE_ARC"
        assert harness.led["active_leds"] >= 1

        # 4. CUP_PRESENT (after 3s arc expires -> AMBIENT_WARM)
        harness.advance_time(duration_ms=3100, step_ms=100)
        assert harness.led["mode"] == "AMBIENT_WARM"
        assert harness.led["active_leds"] == 16

        # 5. CUP_LIFTED state -> LIFTED_GLOW
        harness.lift_cup(duration_ms=500)
        assert_state(harness.telemetry, "CUP_LIFTED")
        assert harness.led["mode"] == "LIFTED_GLOW"
        assert harness.led["active_leds"] == 16

        # 6. Return cup with drink (-100ml) -> RIPPLE_DRINK (1s) -> INTAKE_ARC (3s) -> AMBIENT_WARM
        harness.return_cup(weight_g=200.0, duration_ms=1600)
        assert_event(harness.events, "DRINK_LOGGED")
        assert harness.led["mode"] == "RIPPLE_DRINK"
        assert harness.led["is_alert"] is False

        harness.advance_time(duration_ms=1100, step_ms=100)
        assert harness.led["mode"] == "INTAKE_ARC"
        assert harness.led["active_leds"] >= 1

        harness.advance_time(duration_ms=3100, step_ms=100)
        assert harness.led["mode"] == "AMBIENT_WARM"
        assert harness.led["active_leds"] == 16

        # 7. ALERT_DUE -> ALERT_BREATHING, then ALERT_REPEAT -> ALERT_URGENT
        harness.advance_time(duration_ms=2705 * 1000, step_ms=5000)
        assert harness.led["mode"] == "ALERT_BREATHING"
        assert harness.led["is_alert"] is True

        harness.advance_time(duration_ms=305 * 1000, step_ms=5000)
        assert harness.led["mode"] == "ALERT_URGENT"
        assert harness.led["is_alert"] is True
