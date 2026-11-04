"""Tier 1 - Feature 7: Refill Detection Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_event,
    assert_no_event,
    assert_consumed_ml,
)


class TestT1F7RefillDetection:
    """Test suite for Feature 7 refill detection logic."""

    def test_moderate_refill_50g_detected(self):
        """Verify adding 50g water triggers REFILL_DETECTED."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=200.0, duration_ms=2000)
        harness.refill_cup(refill_ml=50.0, duration_ms=2000)

        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_event(harness.events, "REFILL_DETECTED", {"refill_delta_ml": 50.0})

    def test_large_refill_300g_detected(self):
        """Verify refilling empty bottle with 300g water is recognized."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=150.0, duration_ms=2000)
        harness.refill_cup(refill_ml=300.0, duration_ms=2000)

        assert_event(harness.events, "REFILL_DETECTED", {"refill_delta_ml": 300.0})

    def test_refill_does_not_increment_consumed_volume(self):
        """Verify refilling beverage does NOT count as drinking water."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=200.0, duration_ms=2000)
        harness.refill_cup(refill_ml=250.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 0.0)
        assert_no_event(harness.events, "DRINK_LOGGED")

    def test_refill_does_not_reset_reminder_timer(self):
        """Verify refilling cup does not restart the 45m reminder countdown."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=200.0, duration_ms=2000)

        # Advance 30 minutes (1800 seconds)
        harness.advance_time(duration_ms=1800 * 1000, step_ms=2000)
        countdown_before = harness.telemetry["countdown_s"]

        harness.refill_cup(refill_ml=200.0, duration_ms=2000)
        countdown_after = harness.telemetry["countdown_s"]

        # Countdown must NOT be reset to 2700; it must continue from ~890s
        assert countdown_after < 910
        assert abs(countdown_before - countdown_after) <= 10

    def test_baseline_weight_updates_to_new_refill_weight(self):
        """Verify subsequent drinks compare against the new filled baseline."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=200.0, duration_ms=2000)

        # Refill +200g -> weight is now 400g
        harness.refill_cup(refill_ml=200.0, duration_ms=2000)
        evt_refill = assert_event(harness.events, "REFILL_DETECTED")
        assert abs(evt_refill["event_data"]["baseline_g"] - 400.0) <= 0.5

        # Take 100g drink from refilled cup -> weight 300g (100g lighter than 400g)
        harness.take_drink(sip_ml=100.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 100.0)
        assert_event(harness.events, "DRINK_LOGGED", {"drink_delta_ml": 100.0})
