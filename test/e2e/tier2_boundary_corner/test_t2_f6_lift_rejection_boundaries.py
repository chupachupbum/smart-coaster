"""Tier 2 - Feature 6: Lift Rejection Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_event,
    assert_no_event,
    assert_consumed_ml,
)


class TestT2F6LiftRejectionBoundaries:
    """Boundary test suite for Feature 6 fidget / lift without drink limits."""

    def test_rejection_at_exact_zero_grams(self):
        """Verify returning cup at exact identical weight emits LIFT_NO_DRINK."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=350.0, duration_ms=2000)
        harness.lift_cup(duration_ms=1000)
        harness.return_cup(weight_g=350.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 0.0)
        assert_event(harness.events, "LIFT_NO_DRINK")

    def test_rejection_at_upper_boundary_14_95g(self):
        """Verify delta of 14.95g is the highest rejected non-drink."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.lift_cup(duration_ms=1000)
        harness.return_cup(weight_g=385.05, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 0.0)
        assert_event(harness.events, "LIFT_NO_DRINK")
        assert_no_event(harness.events, "DRINK_LOGGED")

    def test_rejection_of_small_weight_gain_10g(self):
        """Verify returning cup 10g heavier (e.g. ice cube added) is LIFT_NO_DRINK."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=300.0, duration_ms=2000)
        harness.lift_cup(duration_ms=1000)
        harness.return_cup(weight_g=310.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 0.0)
        assert_event(harness.events, "LIFT_NO_DRINK")
        assert_no_event(harness.events, "REFILL_DETECTED")

    def test_rejection_of_near_refill_gain_29_90g(self):
        """Verify returning cup 29.90g heavier fails refill threshold (<30g) and is LIFT_NO_DRINK."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=300.0, duration_ms=2000)
        harness.lift_cup(duration_ms=1000)
        harness.return_cup(weight_g=329.90, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 0.0)
        assert_event(harness.events, "LIFT_NO_DRINK")
        assert_no_event(harness.events, "REFILL_DETECTED")

    def test_active_alert_not_cleared_by_rejected_lift(self):
        """Verify active reminder alert is NOT cleared when user lifts cup without drinking."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        # Trigger reminder alert (45m)
        harness.advance_time(duration_ms=2705 * 1000, step_ms=5000)
        assert harness.telemetry["alert"] is True

        # User lifts cup and puts it back 5g lighter
        harness.lift_cup(duration_ms=1000)
        harness.return_cup(weight_g=395.0, duration_ms=2000)

        # Alert must still be active!
        assert harness.telemetry["alert"] is True
        assert harness.telemetry["countdown_s"] == 0
