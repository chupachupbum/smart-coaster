"""Tier 1 - Feature 5: Valid Drink Logging Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_event,
    assert_consumed_ml,
    assert_notification_sent,
)


class TestT1F5DrinkLogging:
    """Test suite for Feature 5 drink logging and intake accumulation."""

    def test_single_drink_50ml_logged(self):
        """Verify taking a 50ml drink increments consumed volume and drink count."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.take_drink(sip_ml=50.0, duration_ms=2000)

        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_consumed_ml(harness.telemetry, 50.0)
        assert_event(harness.events, "DRINK_LOGGED", {"drink_delta_ml": 50.0})

    def test_large_drink_180ml_logged(self):
        """Verify taking a 180ml drink is properly recorded."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=500.0, duration_ms=2000)
        harness.take_drink(sip_ml=180.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 180.0)
        assert_event(harness.events, "DRINK_LOGGED", {"drink_delta_ml": 180.0})

    def test_multiple_drinks_accumulate_correctly(self):
        """Verify multiple sips (60ml, then 40ml, then 100ml) accumulate to 200ml."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=500.0, duration_ms=2000)

        harness.take_drink(sip_ml=60.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 60.0)

        harness.take_drink(sip_ml=40.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 100.0)

        harness.take_drink(sip_ml=100.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 200.0)

    def test_drink_resets_reminder_countdown_to_full_interval(self):
        """Verify logging a drink resets reminder timer back to 45 minutes (2700s)."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Advance 20 minutes (1200 seconds)
        harness.advance_time(duration_ms=1200 * 1000, step_ms=2000)
        countdown_before = harness.telemetry["countdown_s"]
        assert countdown_before <= 1510

        # Now take a 40ml drink
        harness.take_drink(sip_ml=40.0, duration_ms=2000)
        countdown_after = harness.telemetry["countdown_s"]
        assert 2690 <= countdown_after <= 2700

    def test_drink_dispatches_http_push_notification(self):
        """Verify DRINK_LOGGED generates push notification with priority 3 and droplet tag."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=350.0, duration_ms=2000)
        harness.take_drink(sip_ml=75.0, duration_ms=2000)

        assert_notification_sent(
            harness.notifications,
            title="Drink Logged - Smart Coaster",
            priority=3,
            tag_contains="droplet",
        )
