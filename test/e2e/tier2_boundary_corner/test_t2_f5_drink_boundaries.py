"""Tier 2 - Feature 5: Drink Logging Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_event,
    assert_no_event,
    assert_consumed_ml,
)


class TestT2F5DrinkBoundaries:
    """Boundary test suite for Feature 5 drink delta thresholds."""

    def test_drink_exact_boundary_15_00g_logged(self):
        """Verify delta of exactly 15.00g is accepted as a valid drink."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.take_drink(sip_ml=15.00, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 15.00)
        assert_event(harness.events, "DRINK_LOGGED", {"drink_delta_ml": 15.0})

    def test_drink_sub_threshold_14_90g_rejected(self):
        """Verify delta of 14.90g (<15.0g) is rejected as a drink."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.take_drink(sip_ml=14.90, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 0.0)
        assert_no_event(harness.events, "DRINK_LOGGED")
        assert_event(harness.events, "LIFT_NO_DRINK")

    def test_drink_maximum_scale_chug_1000g(self):
        """Verify massive 1000ml gulp is recorded cleanly."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=1500.0, duration_ms=2000)
        harness.take_drink(sip_ml=1000.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 1000.0)
        assert_event(harness.events, "DRINK_LOGGED", {"drink_delta_ml": 1000.0})

    def test_drink_hitting_goal_at_exact_boundary(self):
        """Verify drinks bringing total consumed to exactly 2000ml trigger GOAL_REACHED."""
        harness = E2ETestHarness(daily_goal_ml=2000.0)
        harness.place_cup(weight_g=1000.0, duration_ms=2000)

        # Drink 1: 500ml -> 500ml
        harness.take_drink(sip_ml=500.0, duration_ms=2000)
        harness.refill_cup(refill_ml=500.0, duration_ms=2000)

        # Drink 2: 500ml -> 1000ml
        harness.take_drink(sip_ml=500.0, duration_ms=2000)
        harness.refill_cup(refill_ml=500.0, duration_ms=2000)

        # Drink 3: 500ml -> 1500ml
        harness.take_drink(sip_ml=500.0, duration_ms=2000)
        harness.refill_cup(refill_ml=500.0, duration_ms=2000)

        assert harness.telemetry["progress_pct"] < 100.0

        # Final drink: 500ml -> brings total to 2000ml (100.0%)
        harness.take_drink(sip_ml=500.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 2000.0)
        assert abs(harness.telemetry["progress_pct"] - 100.0) <= 0.1
        assert_event(harness.events, "GOAL_REACHED")

    def test_drink_clears_alert_due_state(self):
        """Verify taking a valid drink transitions ALERT_DUE back to COUNTING."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        # Trigger alert (45m)
        harness.advance_time(duration_ms=2702 * 1000, step_ms=5000)
        assert harness.telemetry["alert"] is True

        # Drink 25ml
        harness.take_drink(sip_ml=25.0, duration_ms=2000)
        assert harness.telemetry["alert"] is False
        assert 2690 <= harness.telemetry["countdown_s"] <= 2700
