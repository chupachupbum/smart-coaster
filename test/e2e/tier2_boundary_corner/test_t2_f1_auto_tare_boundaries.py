"""Tier 2 - Feature 1: Auto-Tare Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_event,
    assert_no_event,
    assert_weight_g,
)


class TestT2F1AutoTareBoundaries:
    """Boundary test suite for Feature 1 auto-tare edge conditions."""

    def test_auto_tare_exact_positive_boundary_5g(self):
        """Verify exact +5.00g threshold fires auto-tare after 5000ms."""
        harness = E2ETestHarness()
        harness.tick(duration_ms=5100, step_ms=50, weight_g=5.0)

        assert_weight_g(harness.telemetry, 0.0)
        assert_event(harness.events, "AUTO_TARE", {"tare_offset": 5.0})

    def test_auto_tare_positive_boundary_rejection_at_5_05g(self):
        """Verify +5.05g exceeds the +/-5g window and does NOT auto-tare."""
        harness = E2ETestHarness()
        harness.tick(duration_ms=6000, step_ms=50, weight_g=5.05)

        assert_weight_g(harness.telemetry, 5.05)
        assert_no_event(harness.events, "AUTO_TARE")

    def test_auto_tare_exact_negative_boundary_minus_5g(self):
        """Verify exact -5.00g threshold fires auto-tare after 5000ms."""
        harness = E2ETestHarness()
        harness.tick(duration_ms=5100, step_ms=50, weight_g=-5.0)

        assert_weight_g(harness.telemetry, 0.0)
        assert_event(harness.events, "AUTO_TARE", {"tare_offset": -5.0})

    def test_auto_tare_negative_boundary_rejection_at_minus_5_05g(self):
        """Verify -5.05g exceeds negative window and does NOT auto-tare."""
        harness = E2ETestHarness()
        harness.tick(duration_ms=6000, step_ms=50, weight_g=-5.05)

        assert_weight_g(harness.telemetry, -5.05)
        assert_no_event(harness.events, "AUTO_TARE")

    def test_auto_tare_timing_boundary_4950ms_vs_5050ms(self):
        """Verify no tare at 4950ms; tare occurs immediately after passing 5000ms."""
        harness = E2ETestHarness()
        # Feed +3.0g for 4950ms
        harness.tick(duration_ms=4950, step_ms=50, weight_g=3.0)
        assert_no_event(harness.events, "AUTO_TARE")
        assert_weight_g(harness.telemetry, 3.0)

        # Advance another 100ms (total 5050ms)
        harness.tick(duration_ms=100, step_ms=50, weight_g=3.0)
        assert_event(harness.events, "AUTO_TARE", {"tare_offset": 3.0})
        assert_weight_g(harness.telemetry, 0.0)

    def test_auto_tare_noise_spike_resets_continuous_timer(self):
        """Verify temporary spike to +6.0g at 4.5s resets the 5s continuous requirement."""
        harness = E2ETestHarness()
        # 4500ms at +3.0g
        harness.tick(duration_ms=4500, step_ms=50, weight_g=3.0)
        # Spike to +6.0g for 100ms
        harness.tick(duration_ms=100, step_ms=50, weight_g=6.0)
        # Return to +3.0g for 3000ms (total elapsed 7.6s, but continuous after spike only 3.0s)
        harness.tick(duration_ms=3000, step_ms=50, weight_g=3.0)

        assert_no_event(harness.events, "AUTO_TARE")
        assert_weight_g(harness.telemetry, 3.0)

    def test_auto_tare_strictly_disabled_in_cup_present_state(self):
        """Verify auto-tare never triggers while cup is present, even if stable within 1g."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=350.0, duration_ms=2000)

        # Let cup sit with minor +/-0.2g fluctuation for 10 seconds
        harness.tick(duration_ms=10000, step_ms=100, weight_g=350.1)

        # Tare offset remains 0.0
        assert_no_event(harness.events, "AUTO_TARE")
        assert harness.telemetry["weight_g"] >= 300.0
