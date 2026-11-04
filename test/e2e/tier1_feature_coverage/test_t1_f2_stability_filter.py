"""Tier 1 - Feature 2: Weight Stability Filter Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import E2ETestHarness


class TestT1F2StabilityFilter:
    """Test suite for Feature 2 stability filtering logic."""

    def test_stability_flag_true_when_span_under_3g_for_1500ms(self):
        """Verify stability filter declares stable when reading stays within 1.5g span for 1.5s."""
        harness = E2ETestHarness()
        # Feed 200.0g with small 0.5g ripple for 1600ms
        for _ in range(16):
            harness.tick(duration_ms=50, step_ms=50, weight_g=200.2)
            harness.tick(duration_ms=50, step_ms=50, weight_g=199.8)

        telemetry = harness.telemetry
        assert telemetry["stable"] is True
        assert abs(telemetry["stable_g"] - 200.0) <= 0.3

    def test_stability_flag_false_when_span_exceeds_3g(self):
        """Verify stability filter declares false when readings oscillate by 5.0g."""
        harness = E2ETestHarness()
        for _ in range(16):
            harness.tick(duration_ms=50, step_ms=50, weight_g=205.0)
            harness.tick(duration_ms=50, step_ms=50, weight_g=199.0)

        telemetry = harness.telemetry
        assert telemetry["stable"] is False

    def test_stability_flag_false_before_1500ms_elapsed(self):
        """Verify stability is false before 1500ms even with zero noise."""
        harness = E2ETestHarness()
        # Feed perfectly flat 350.0g for only 1000ms
        harness.tick(duration_ms=1000, step_ms=100, weight_g=350.0)

        telemetry = harness.telemetry
        assert telemetry["stable"] is False

    def test_stability_transition_from_unstable_to_stable(self):
        """Verify system smoothly becomes stable once vibration ceases."""
        harness = E2ETestHarness()
        # Phase 1: High vibration (span 8.0g) for 2000ms
        for _ in range(20):
            harness.tick(duration_ms=50, step_ms=50, weight_g=300.0)
            harness.tick(duration_ms=50, step_ms=50, weight_g=308.0)
        assert harness.telemetry["stable"] is False

        # Phase 2: Settled weight (span 0.2g) for 1600ms
        harness.tick(duration_ms=1600, step_ms=50, weight_g=304.0)
        assert harness.telemetry["stable"] is True
        assert abs(harness.telemetry["stable_g"] - 304.0) <= 0.2

    def test_stability_calculates_correct_arithmetic_mean(self):
        """Verify stable_g is calculated as the arithmetic mean of the window."""
        harness = E2ETestHarness()
        # Feed alternating 400.0 and 402.0 (span 2.0g <= 3.0g)
        for _ in range(16):
            harness.tick(duration_ms=50, step_ms=50, weight_g=400.0)
            harness.tick(duration_ms=50, step_ms=50, weight_g=402.0)

        telemetry = harness.telemetry
        assert telemetry["stable"] is True
        # Mean of equal 400.0 and 402.0 samples is 401.0
        assert abs(telemetry["stable_g"] - 401.0) <= 0.2
