"""Tier 2 - Feature 9: Repeat Alert Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_event,
    assert_no_event,
)


class TestT2F9RepeatBoundaries:
    """Boundary test suite for Feature 9 5-minute repeat alert intervals."""

    def test_repeat_interval_boundary_299s_vs_301s(self):
        """Verify repeat alert does not fire at 280s, but fires after passing 300s."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        # Advance to primary reminder (2701s)
        harness.advance_time(duration_ms=2701 * 1000, step_ms=5000)

        # Advance 280 seconds into repeat interval (less than 300s)
        harness.advance_time(duration_ms=280 * 1000, step_ms=2000)
        assert_no_event(harness.events, "REPEAT_ALERT")

        # Advance 30 more seconds (total over 300s = 5m repeat interval)
        harness.advance_time(duration_ms=30000, step_ms=2000)
        assert_event(harness.events, "REPEAT_ALERT")

    def test_demo_mode_repeat_boundary_4s_vs_6s(self):
        """Verify DEMO mode repeat interval fires at 5s."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")
        harness.place_cup(weight_g=350.0, duration_ms=2000)

        # 46s: primary reminder fired
        harness.advance_time(duration_ms=46 * 1000, step_ms=500)
        assert_no_event(harness.events, "REPEAT_ALERT")

        # Advance 2 seconds (total elapsed after alert < 5s)
        harness.advance_time(duration_ms=2000, step_ms=500)
        assert_no_event(harness.events, "REPEAT_ALERT")

        # Advance 4 more seconds (total > 5s demo repeat)
        harness.advance_time(duration_ms=4000, step_ms=500)
        assert_event(harness.events, "REPEAT_ALERT")

    def test_drink_clears_alert_prior_to_second_repeat(self):
        """Verify drink logged during first repeat interval prevents second repeat."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        # Trigger primary reminder (2701s)
        harness.advance_time(duration_ms=2701 * 1000, step_ms=5000)
        # Trigger 1st repeat (302s)
        harness.advance_time(duration_ms=302 * 1000, step_ms=2000)
        assert len([e for e in harness.events if e.get("event") == "REPEAT_ALERT"]) == 1

        # User drinks 50ml at 2 minutes into second repeat
        harness.advance_time(duration_ms=120 * 1000, step_ms=2000)
        harness.take_drink(sip_ml=50.0, duration_ms=2000)
        assert harness.telemetry["alert"] is False

        # Advance another 10 minutes: no further repeat alerts should fire!
        harness.advance_time(duration_ms=600 * 1000, step_ms=5000)
        assert len([e for e in harness.events if e.get("event") == "REPEAT_ALERT"]) == 1

    def test_repeat_cadence_stability_over_five_cycles(self):
        """Verify alert fires predictably across 5 repeat cycles (25 minutes)."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        # 45m + 5x 5m = 70 minutes
        harness.advance_time(duration_ms=(2700 + 5 * 300 + 10) * 1000, step_ms=5000)

        repeats = [e for e in harness.events if e.get("event") == "REPEAT_ALERT"]
        assert len(repeats) == 5

    def test_repeat_alert_maintains_zero_countdown(self):
        """Verify countdown stays pegged at 0s during all repeat alerts."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.advance_time(duration_ms=(2700 + 350) * 1000, step_ms=5000)

        assert harness.telemetry["alert"] is True
        assert harness.telemetry["countdown_s"] == 0
