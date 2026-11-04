"""Tier 1 - Feature 9: 5-Minute Repeating Alert Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_event,
    assert_notification_sent,
)


class TestT1F9RepeatAlert:
    """Test suite for Feature 9 5-minute repeating reminder alerts."""

    def test_repeat_alert_fires_5_minutes_after_primary_reminder(self):
        """Verify repeat alert triggers 5 minutes (300s) after primary reminder."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Trigger primary reminder (45m + 1s)
        harness.advance_time(duration_ms=2701 * 1000, step_ms=5000)
        assert_event(harness.events, "REMINDER_ALERT")

        # Advance 5 minutes + 2 seconds (302 seconds)
        harness.advance_time(duration_ms=302 * 1000, step_ms=2000)
        assert_event(harness.events, "REPEAT_ALERT")

    def test_repeat_alert_dispatches_priority_4_notification(self):
        """Verify repeat alert sends notification with tag repeat."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.advance_time(duration_ms=(2700 + 305) * 1000, step_ms=5000)

        assert_notification_sent(
            harness.notifications,
            title="Hydration Alert (Repeat)",
            priority=4,
            tag_contains="repeat",
        )

    def test_multiple_repeat_cycles_fire_sequentially(self):
        """Verify alerts continue repeating every 5 minutes (e.g. 3 cycles)."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # 45m primary + 15m (3x 5m repeats) = 60 minutes
        harness.advance_time(duration_ms=(2700 + 300 * 3 + 10) * 1000, step_ms=5000)

        repeats = [e for e in harness.events if e.get("event") == "REPEAT_ALERT"]
        assert len(repeats) >= 3

    def test_demo_mode_accelerates_repeat_to_5_seconds(self):
        """Verify DEMO mode scales repeat interval to 5s."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")
        harness.place_cup(weight_g=350.0, duration_ms=2000)

        # 45s primary + 6s repeat
        harness.advance_time(duration_ms=52 * 1000, step_ms=500)
        assert_event(harness.events, "REPEAT_ALERT")

    def test_valid_drink_clears_active_repeat_alert(self):
        """Verify logging a drink while in repeat alert clears alert state."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.advance_time(duration_ms=(2700 + 310) * 1000, step_ms=5000)
        assert harness.telemetry["alert"] is True

        # Drink 100ml
        harness.take_drink(sip_ml=100.0, duration_ms=2000)
        assert harness.telemetry["alert"] is False
        assert 2690 <= harness.telemetry["countdown_s"] <= 2700
