"""Tier 2 - Feature 3: Cup Presence Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_no_event,
)


class TestT2F3PresenceBoundaries:
    """Boundary test suite for Feature 3 cup presence detection thresholds."""

    def test_presence_exact_boundary_rejection_at_30_00g(self):
        """Verify weight of exactly 30.00g fails the >30.0g threshold and remains EMPTY."""
        harness = E2ETestHarness()
        harness.tick(duration_ms=2500, step_ms=50, weight_g=30.00)

        assert_state(harness.telemetry, "EMPTY")
        assert_no_event(harness.events, "STATE_CHANGE")

    def test_presence_exact_boundary_acceptance_at_30_10g(self):
        """Verify weight of 30.10g (>30g) transitions to CUP_PRESENT once stabilized."""
        harness = E2ETestHarness()
        harness.tick(duration_ms=2000, step_ms=50, weight_g=30.10)

        assert_state(harness.telemetry, "CUP_PRESENT")

    def test_presence_aborted_when_weight_drops_during_stabilizing(self):
        """Verify cup briefly touched (drops below 30g at 1.0s) reverts to EMPTY."""
        harness = E2ETestHarness()
        # Placed for 1000ms
        harness.tick(duration_ms=1000, step_ms=50, weight_g=300.0)
        assert_state(harness.telemetry, "STABILIZING")

        # Removed back to 0.0g
        harness.tick(duration_ms=500, step_ms=50, weight_g=0.0)
        assert_state(harness.telemetry, "EMPTY")

    def test_presence_delayed_by_surface_vibration(self):
        """Verify presence transition waits until vibration settles below 3g span."""
        harness = E2ETestHarness()
        # Vibrate for 3 seconds (span 6g)
        for _ in range(30):
            harness.tick(duration_ms=50, step_ms=50, weight_g=350.0)
            harness.tick(duration_ms=50, step_ms=50, weight_g=356.0)

        # Still stabilizing because span > 3g
        assert_state(harness.telemetry, "STABILIZING")

        # Now settle for 1600ms
        harness.tick(duration_ms=1600, step_ms=50, weight_g=353.0)
        assert_state(harness.telemetry, "CUP_PRESENT")

    def test_presence_threshold_accounts_for_tare_offset(self):
        """Verify net weight after tare offset is used for presence comparison."""
        harness = E2ETestHarness()
        # First tare an offset of +4.0g
        harness.tick(duration_ms=5100, step_ms=50, weight_g=4.0)

        # Now raw weight becomes 33.0g. Net weight = 33.0 - 4.0 = 29.0g (<30g)
        harness.tick(duration_ms=2000, step_ms=50, weight_g=33.0)
        assert_state(harness.telemetry, "EMPTY")

        # Now raw weight becomes 35.0g. Net weight = 35.0 - 4.0 = 31.0g (>30g)
        harness.tick(duration_ms=2000, step_ms=50, weight_g=35.0)
        assert_state(harness.telemetry, "CUP_PRESENT")
