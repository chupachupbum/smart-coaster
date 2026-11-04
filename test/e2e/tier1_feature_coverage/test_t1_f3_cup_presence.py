"""Tier 1 - Feature 3: Cup Presence Detection Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_event,
    assert_weight_g,
)


class TestT1F3CupPresence:
    """Test suite for Feature 3 cup placement and presence detection."""

    def test_cup_placement_transitions_to_stabilizing_then_cup_present(self):
        """Verify placing 350g cup transitions EMPTY -> STABILIZING -> CUP_PRESENT."""
        harness = E2ETestHarness()
        assert_state(harness.telemetry, "EMPTY")

        # Step 1: Initial placement reading
        harness.tick(duration_ms=100, step_ms=50, weight_g=350.0)
        assert_state(harness.telemetry, "STABILIZING")

        # Step 2: Settle for 1600ms
        harness.tick(duration_ms=1600, step_ms=50, weight_g=350.0)
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_event(harness.events, "STATE_CHANGE", {"old_state": "STABILIZING", "new_state": "CUP_PRESENT"})

    def test_heavy_bottle_presence_detected(self):
        """Verify 800g water bottle is recognized as CUP_PRESENT."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=800.0, duration_ms=2000)

        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_weight_g(harness.telemetry, 800.0)

    def test_lightweight_cup_presence_detected(self):
        """Verify lightweight 45g espresso cup (>30g) is recognized as CUP_PRESENT."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=45.0, duration_ms=2000)

        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_weight_g(harness.telemetry, 45.0)

    def test_baseline_weight_locked_on_placement(self):
        """Verify baseline_weight_g matches settled weight upon presence confirmation."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=420.0, duration_ms=2000)

        # Baseline recorded in state change event
        evt = assert_event(harness.events, "STATE_CHANGE", {"new_state": "CUP_PRESENT"})
        assert abs(evt["event_data"]["baseline_g"] - 420.0) <= 0.5

    def test_reminder_countdown_initialized_on_presence(self):
        """Verify 45-minute countdown (2700s) starts upon entering CUP_PRESENT."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=300.0, duration_ms=2000)

        countdown = harness.telemetry["countdown_s"]
        assert 2690 <= countdown <= 2700
