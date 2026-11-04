"""Tier 1 - Feature 1: Empty State Auto-Tare Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_event,
    assert_weight_g,
)


class TestT1F1AutoTare:
    """Test suite for Feature 1 nominal auto-tare behavior."""

    def test_auto_tare_positive_offset_applies_after_5s(self):
        """Verify positive resting drift (+2.5g) for 5 seconds triggers auto-tare."""
        harness = E2ETestHarness()
        # Feed +2.5g for 5.1 seconds
        harness.tick(duration_ms=5100, step_ms=100, weight_g=2.5)

        assert_state(harness.telemetry, "EMPTY")
        assert_weight_g(harness.telemetry, 0.0)
        assert_event(harness.events, "AUTO_TARE", {"tare_offset": 2.5})

    def test_auto_tare_negative_offset_applies_after_5s(self):
        """Verify negative resting drift (-3.0g) for 5 seconds triggers auto-tare."""
        harness = E2ETestHarness()
        # Feed -3.0g for 5.1 seconds
        harness.tick(duration_ms=5100, step_ms=100, weight_g=-3.0)

        assert_state(harness.telemetry, "EMPTY")
        assert_weight_g(harness.telemetry, 0.0)
        assert_event(harness.events, "AUTO_TARE", {"tare_offset": -3.0})

    def test_auto_tare_demo_mode_accelerates_to_1s(self):
        """Verify DEMO mode accelerates auto-tare from 5.0s to 1.0s."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")

        # Feed +4.0g for 1.1 seconds in demo mode
        harness.tick(duration_ms=1100, step_ms=50, weight_g=4.0)

        assert_state(harness.telemetry, "EMPTY")
        assert_weight_g(harness.telemetry, 0.0)
        assert_event(harness.events, "AUTO_TARE", {"tare_offset": 4.0})

    def test_auto_tare_cumulative_drift_tracking(self):
        """Verify consecutive small drifts (+2.0g then +1.5g) accumulate cleanly."""
        harness = E2ETestHarness()
        # First drift +2.0g for 5.1s
        harness.tick(duration_ms=5100, step_ms=100, weight_g=2.0)
        assert_weight_g(harness.telemetry, 0.0)

        # Second drift: physical raw reading becomes +3.5g (additional 1.5g)
        harness.tick(duration_ms=5100, step_ms=100, weight_g=3.5)
        assert_weight_g(harness.telemetry, 0.0)
        assert_event(harness.events, "AUTO_TARE", {"tare_offset": 3.5})

    def test_auto_tare_no_trigger_before_5s_elapsed(self):
        """Verify auto-tare does not trigger when weight is held for only 4.0 seconds."""
        harness = E2ETestHarness()
        harness.tick(duration_ms=4000, step_ms=100, weight_g=2.0)

        # At 4s, tare offset remains 0.0 and net weight remains 2.0
        assert_weight_g(harness.telemetry, 2.0)
        events = [e for e in harness.events if e.get("event") == "AUTO_TARE"]
        assert len(events) == 0
