"""Tier 2 - Feature 4: Cup Lift Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
)


class TestT2F4LiftBoundaries:
    """Boundary test suite for Feature 4 cup lift thresholds."""

    def test_lift_exact_boundary_rejection_at_20_00g(self):
        """Verify weight at exactly 20.00g does NOT trigger lift (remains CUP_PRESENT)."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Weight drops to exactly 20.00g (hand resting on cup, etc.)
        harness.tick(duration_ms=1000, step_ms=50, weight_g=20.00)
        assert_state(harness.telemetry, "CUP_PRESENT")

    def test_lift_exact_boundary_trigger_at_19_90g(self):
        """Verify weight dropping to 19.90g (<20g) triggers CUP_LIFTED."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        harness.tick(duration_ms=500, step_ms=50, weight_g=19.90)
        assert_state(harness.telemetry, "CUP_LIFTED")

    def test_rapid_touch_and_release_cycle(self):
        """Verify quick hand movement (<200ms) does not corrupt state tracking."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Drop to 10g for only 150ms then back to 400g
        harness.tick(duration_ms=150, step_ms=50, weight_g=10.0)
        assert_state(harness.telemetry, "CUP_LIFTED")

        harness.return_cup(weight_g=400.0, duration_ms=2000)
        assert_state(harness.telemetry, "CUP_PRESENT")

    def test_prolonged_cup_lift_indefinite_session(self):
        """Verify cup held for 30 minutes maintains CUP_LIFTED and ticks countdown."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.lift_cup(duration_ms=500)

        # Held for 30 minutes (1800 seconds)
        harness.advance_time(duration_ms=1800 * 1000, step_ms=5000)
        assert_state(harness.telemetry, "CUP_LIFTED")
        assert harness.telemetry["countdown_s"] < 910

    def test_weight_fluctuating_above_20g_remains_present(self):
        """Verify tea cup stirred with spoon fluctuating 21g-25g stays in CUP_PRESENT."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=100.0, duration_ms=2000)

        for _ in range(10):
            harness.tick(duration_ms=100, step_ms=50, weight_g=21.5)
            harness.tick(duration_ms=100, step_ms=50, weight_g=24.0)

        assert_state(harness.telemetry, "CUP_PRESENT")
