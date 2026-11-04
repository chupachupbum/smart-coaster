"""Tier 5 - Timing, Drift, and Burst Adversarial Hardening Tests.

Adversarial challenges:
- Sub-gram drift around AUTO_TARE_MIN_DRIFT_G (0.04g vs 0.06g)
- Auto-tare timer cancellation by boundary breach at 4.9s
- Exact millisecond transition to REMINDER_ALERT
- Inactivity alert persistence across lift-without-drinking
- Active alert clearance on valid drink
- Goal reached single-trigger invariant across overshoot
- Rapid event burst processing
"""

import pytest
from test.e2e.framework import E2ETestHarness
from test.e2e.framework.assertions import (
    assert_state,
    assert_event,
    assert_no_event,
    assert_consumed_ml,
    assert_weight_g,
)


class TestT5TimingDriftBursts:
    """Adversarial testing of timing windows, sensor drift boundaries, and alert states."""

    def test_subgram_drift_under_min_threshold_ignored(self):
        """Precision: Constant 0.04g drift for 6.0s.
        Below AUTO_TARE_MIN_DRIFT_G (0.05g), so auto-tare must NOT trigger."""
        harness = E2ETestHarness()

        # Feed 0.04g for 6 seconds (120 ticks @ 50ms)
        harness.tick(duration_ms=6000, step_ms=50, weight_g=0.04)

        assert_no_event(harness.events, "AUTO_TARE")
        assert_state(harness.telemetry, "EMPTY")

    def test_subgram_drift_above_min_threshold_applies_tare(self):
        """Precision: Constant 0.06g drift for 5.1s.
        Above AUTO_TARE_MIN_DRIFT_G (0.05g) and below 5.0g, so auto-tare triggers at 5.0s."""
        harness = E2ETestHarness()

        # Feed 0.06g for 5.1 seconds
        harness.tick(duration_ms=5100, step_ms=50, weight_g=0.06)

        assert_event(harness.events, "AUTO_TARE")
        # Net weight tared to 0.0g
        assert_weight_g(harness.telemetry, 0.0, tolerance=0.05)

    def test_autotare_timer_aborted_by_transient_noise_spike(self):
        """Noise: +3.0g drift for 4.8 seconds (almost complete), then transient spike to +5.5g at 4.9s.
        Verifies auto-tare timer resets and does not falsely tare upon spike."""
        harness = E2ETestHarness()

        # 4.8 seconds of 3.0g drift
        harness.tick(duration_ms=4800, step_ms=50, weight_g=3.0)
        assert_no_event(harness.events, "AUTO_TARE")

        # Noise spike to 5.5g (> 5.0g window) for 100ms
        harness.tick(duration_ms=100, step_ms=50, weight_g=5.5)
        assert_no_event(harness.events, "AUTO_TARE")

        # Settle back to 3.0g for 2.0s (total elapsed since start is 6.9s, but only 2.0s since reset)
        harness.tick(duration_ms=2000, step_ms=50, weight_g=3.0)
        assert_no_event(harness.events, "AUTO_TARE")

        # Advance remaining 3.1s to allow new timer to complete
        harness.tick(duration_ms=3100, step_ms=50, weight_g=3.0)
        assert_event(harness.events, "AUTO_TARE")

    def test_alert_persistence_across_lift_without_drinking(self):
        """Contract: When in ALERT_DUE or ALERT_REPEAT, lifting and returning cup
        without at least 15g reduction MUST NOT silence the alert."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")
        harness.place_cup(300.0)

        # Trigger inactivity reminder (45s in demo mode)
        harness.advance_time(46000)
        assert harness.telemetry["alert"] is True
        assert_event(harness.events, "REMINDER_ALERT")

        # User lifts cup, fidgets, and puts it back with 0g delta (no drink)
        harness.lift_cup(1500)
        harness.return_cup(300.0, duration_ms=2000)

        # Alert MUST STILL BE ACTIVE!
        assert harness.telemetry["alert"] is True
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_consumed_ml(harness.telemetry, 0.0)

    def test_alert_cleared_immediately_on_valid_drink(self):
        """Contract: When alert is active (ALERT_REPEAT), taking a valid drink (>=15g)
        immediately clears alert, sets alert=False, and restarts 45m timer."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")
        harness.place_cup(300.0)

        # Trigger repeat alert (45s + 6s = 51s)
        harness.advance_time(51000)
        assert harness.telemetry["alert"] is True

        # Now take a valid drink of 25g
        harness.take_drink(25.0)

        # Alert must be cleared immediately!
        assert harness.telemetry["alert"] is False
        assert_consumed_ml(harness.telemetry, 25.0)
        # Countdown restarted: in demo mode, reminder is 45s
        assert harness.telemetry["countdown_s"] > 35

    def test_goal_reached_single_fire_on_overshoot(self):
        """Contract: GOAL_REACHED event must be emitted exactly once when daily intake
        crosses daily_goal_ml, and must NOT fire again on subsequent drinks."""
        harness = E2ETestHarness(daily_goal_ml=500.0)
        # Place 600g cup (e.g. 90g empty cup + 510g water)
        harness.place_cup(600.0)

        # Drink 1: 250ml (consumed: 250ml / 500ml = 50%, cup remains at 350g)
        harness.take_drink(250.0)
        assert_consumed_ml(harness.telemetry, 250.0)
        assert_no_event(harness.events, "GOAL_REACHED")

        # Drink 2: 260ml (consumed: 510ml / 500ml = 102% - goal reached!, cup remains at 90g > 30g)
        harness.take_drink(260.0)
        assert_consumed_ml(harness.telemetry, 510.0)
        assert_event(harness.events, "GOAL_REACHED")

        goal_events_count = len([e for e in harness.events if e.get("event") == "GOAL_REACHED"])
        assert goal_events_count == 1

        # Drink 3: 50ml additional after 200ml refill
        harness.refill_cup(200.0) # cup at 290g
        harness.take_drink(50.0)   # cup at 240g, consumed = 560ml
        assert_consumed_ml(harness.telemetry, 560.0)

        # GOAL_REACHED must still be emitted only ONCE
        goal_events_count_after = len([e for e in harness.events if e.get("event") == "GOAL_REACHED"])
        assert goal_events_count_after == 1
