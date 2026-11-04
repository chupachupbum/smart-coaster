"""Tier 1 - Feature 8: 45-Minute Hydration Reminder Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_event,
    assert_notification_sent,
    assert_no_event,
)


class TestT1F8ReminderAlert:
    """Test suite for Feature 8 hydration inactivity reminder."""

    def test_reminder_alert_triggers_after_45_minutes_inactivity(self):
        """Verify reminder fires after 45 minutes without drinking."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Advance 45 minutes + 2 seconds (2702 seconds)
        harness.advance_time(duration_ms=2702 * 1000, step_ms=5000)

        telemetry = harness.telemetry
        assert telemetry["alert"] is True
        assert telemetry["countdown_s"] == 0
        assert_event(harness.events, "REMINDER_ALERT")

    def test_reminder_dispatches_priority_4_notification(self):
        """Verify reminder generates high priority push notification to ntfy.sh."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.advance_time(duration_ms=2705 * 1000, step_ms=5000)

        assert_notification_sent(
            harness.notifications,
            title="Hydration Alert",
            priority=4,
            tag_contains="alarm_clock",
        )

    def test_demo_mode_accelerates_reminder_to_45_seconds(self):
        """Verify DEMO mode scales 45m reminder to 45s."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")
        harness.place_cup(weight_g=350.0, duration_ms=2000)

        # Advance 46 seconds in demo mode
        harness.advance_time(duration_ms=46 * 1000, step_ms=500)

        telemetry = harness.telemetry
        assert telemetry["alert"] is True
        assert_event(harness.events, "REMINDER_ALERT")

    def test_no_reminder_alert_if_cup_never_placed(self):
        """Verify coaster resting in EMPTY does not fire hydration alerts."""
        harness = E2ETestHarness()
        # Coaster left empty for 2 hours (7200 seconds)
        harness.tick(duration_ms=7200 * 1000, step_ms=10000, weight_g=0.0)

        assert harness.telemetry["alert"] is False
        assert_no_event(harness.events, "REMINDER_ALERT")

    def test_valid_drink_clears_active_reminder_alert(self):
        """Verify taking a sip clears the alert and resets the countdown."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.advance_time(duration_ms=2705 * 1000, step_ms=5000)
        assert harness.telemetry["alert"] is True

        # User responds by taking 80ml drink
        harness.take_drink(sip_ml=80.0, duration_ms=2000)
        telemetry = harness.telemetry
        assert telemetry["alert"] is False
        assert 2690 <= telemetry["countdown_s"] <= 2700
