"""Tier 2 - Feature 7: Refill Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_event,
    assert_no_event,
    assert_consumed_ml,
)


class TestT2F7RefillBoundaries:
    """Boundary test suite for Feature 7 beverage refill thresholds."""

    def test_refill_exact_boundary_30_00g_detected(self):
        """Verify return weight of exactly +30.00g triggers REFILL_DETECTED."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=200.0, duration_ms=2000)
        harness.refill_cup(refill_ml=30.00, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 0.0)
        assert_event(harness.events, "REFILL_DETECTED", {"refill_delta_ml": 30.0})

    def test_refill_sub_threshold_29_95g_rejected(self):
        """Verify return weight of +29.95g (<30g) does NOT trigger refill."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=200.0, duration_ms=2000)
        harness.refill_cup(refill_ml=29.95, duration_ms=2000)

        assert_no_event(harness.events, "REFILL_DETECTED")
        assert_event(harness.events, "LIFT_NO_DRINK")

    def test_massive_refill_2000g(self):
        """Verify large 2000g water refill scales without numerical overflow."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=200.0, duration_ms=2000)
        harness.refill_cup(refill_ml=2000.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 0.0)
        assert_event(harness.events, "REFILL_DETECTED", {"refill_delta_ml": 2000.0})

    def test_refill_after_goal_achieved(self):
        """Verify refilling cup when goal is already 100% does not break goal state."""
        harness = E2ETestHarness(daily_goal_ml=500.0)
        harness.place_cup(weight_g=600.0, duration_ms=2000)
        harness.take_drink(sip_ml=500.0, duration_ms=2000)
        assert harness.telemetry["progress_pct"] >= 100.0

        # Refill 400ml
        harness.refill_cup(refill_ml=400.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 500.0)
        assert harness.telemetry["progress_pct"] >= 100.0

    def test_consecutive_refills_step_baseline(self):
        """Verify two refills (+50g then +70g) successively increase baseline."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=150.0, duration_ms=2000)

        # Refill 1: +50g -> 200g
        harness.refill_cup(refill_ml=50.0, duration_ms=2000)
        # Refill 2: +70g -> 270g
        harness.refill_cup(refill_ml=70.0, duration_ms=2000)

        refills = [e for e in harness.events if e.get("event") == "REFILL_DETECTED"]
        assert len(refills) == 2
        assert abs(refills[-1]["event_data"]["baseline_g"] - 270.0) <= 0.5
