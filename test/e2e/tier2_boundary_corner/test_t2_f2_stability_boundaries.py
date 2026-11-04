"""Tier 2 - Feature 2: Stability Filter Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import E2ETestHarness


class TestT2F2StabilityBoundaries:
    """Boundary test suite for Feature 2 stability filter limits."""

    def test_stability_exact_span_boundary_3g(self):
        """Verify readings spanning exactly 3.00g (300.0g to 303.0g) are considered stable."""
        harness = E2ETestHarness()
        for _ in range(16):
            harness.tick(duration_ms=50, step_ms=50, weight_g=300.0)
            harness.tick(duration_ms=50, step_ms=50, weight_g=303.0)

        assert harness.telemetry["stable"] is True

    def test_stability_span_rejection_boundary_3_05g(self):
        """Verify readings spanning 3.05g (300.0g to 303.05g) are rejected as unstable."""
        harness = E2ETestHarness()
        for _ in range(16):
            harness.tick(duration_ms=50, step_ms=50, weight_g=300.0)
            harness.tick(duration_ms=50, step_ms=50, weight_g=303.05)

        assert harness.telemetry["stable"] is False

    def test_stability_duration_boundary_1450ms_vs_1550ms(self):
        """Verify signal is unstable at 1400ms and becomes stable at 1600ms."""
        harness = E2ETestHarness()
        # Feed 1400ms
        harness.tick(duration_ms=1400, step_ms=50, weight_g=250.0)
        assert harness.telemetry["stable"] is False

        # Feed additional 200ms (total 1600ms >= 1500ms window)
        harness.tick(duration_ms=200, step_ms=50, weight_g=250.0)
        assert harness.telemetry["stable"] is True

    def test_stability_requires_minimum_samples(self):
        """Verify single isolated sample cannot trigger stability."""
        harness = E2ETestHarness()
        harness.tick(duration_ms=50, step_ms=50, weight_g=300.0)
        assert harness.telemetry["stable"] is False

    def test_stability_recovery_after_large_drop(self):
        """Verify dropping from 400g to 0g purges old readings and settles at 0g."""
        harness = E2ETestHarness()
        harness.tick(duration_ms=2000, step_ms=50, weight_g=400.0)
        assert harness.telemetry["stable"] is True

        # Sudden drop to 0.0g for 500ms: unstable during transition
        harness.tick(duration_ms=500, step_ms=50, weight_g=0.0)
        assert harness.telemetry["stable"] is False

        # Wait remaining 1100ms (total 1600ms at 0g) -> stabilizes at 0g
        harness.tick(duration_ms=1100, step_ms=50, weight_g=0.0)
        assert harness.telemetry["stable"] is True
        assert abs(harness.telemetry["stable_g"] - 0.0) <= 0.1
