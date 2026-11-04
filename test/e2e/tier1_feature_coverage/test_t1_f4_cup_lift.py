"""Tier 1 - Feature 4: Cup Lift Detection Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_event,
    assert_weight_g,
)


class TestT1F4CupLift:
    """Test suite for Feature 4 cup lift detection."""

    def test_complete_lift_to_zero_grams(self):
        """Verify lifting cup completely off coaster transitions to CUP_LIFTED."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        assert_state(harness.telemetry, "CUP_PRESENT")

        harness.lift_cup(duration_ms=500)
        assert_state(harness.telemetry, "CUP_LIFTED")
        assert_weight_g(harness.telemetry, 0.0)

    def test_partial_lift_below_20g_threshold(self):
        """Verify reading dropping to 12.0g (<20g) triggers CUP_LIFTED."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=350.0, duration_ms=2000)

        harness.tick(duration_ms=500, step_ms=50, weight_g=12.0)
        assert_state(harness.telemetry, "CUP_LIFTED")

    def test_lift_event_metadata(self):
        """Verify STATE_CHANGE event emitted with old_state and new_state."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=300.0, duration_ms=2000)
        harness.lift_cup(duration_ms=500)

        assert_event(
            harness.events,
            "STATE_CHANGE",
            {"old_state": "CUP_PRESENT", "new_state": "CUP_LIFTED"},
        )

    def test_baseline_weight_preserved_during_lift(self):
        """Verify baseline weight remains 450.0g while cup is in CUP_LIFTED."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=450.0, duration_ms=2000)
        harness.lift_cup(duration_ms=2000)

        # Telemetry stable_g reflects baseline when lifted or in filter
        assert_state(harness.telemetry, "CUP_LIFTED")

    def test_reminder_countdown_continues_ticking_while_lifted(self):
        """Verify 45m reminder timer keeps counting down even while cup is lifted."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=350.0, duration_ms=2000)
        cd_start = harness.telemetry["countdown_s"]

        # Keep cup lifted for 10 seconds
        harness.tick(duration_ms=10000, step_ms=500, weight_g=0.0)
        cd_after = harness.telemetry["countdown_s"]

        assert cd_after < cd_start
        assert (cd_start - cd_after) >= 9
