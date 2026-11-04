"""Tier 1 - Feature 12: Wi-Fi HTTP Push Notifications Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_notification_sent,
)


class TestT1F12PushNotifications:
    """Test suite for Feature 12 Wi-Fi HTTP push notifications via ntfy.sh."""

    def test_drink_logged_notification_structure(self):
        """Verify drink logged push notification structure and metadata."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.take_drink(sip_ml=120.0, duration_ms=2000)

        notif = assert_notification_sent(harness.notifications, "Drink Logged - Smart Coaster")
        assert notif["priority"] == 3
        assert "droplet" in notif["tags"]
        assert "120ml" in notif["message"]

    def test_reminder_alert_notification_structure(self):
        """Verify reminder alert notification has priority 4 and warning tags."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")
        harness.place_cup(weight_g=350.0, duration_ms=2000)
        harness.advance_time(duration_ms=46 * 1000, step_ms=500)

        notif = assert_notification_sent(harness.notifications, "Hydration Alert")
        assert notif["priority"] == 4
        assert "alarm_clock" in notif["tags"]

    def test_repeat_alert_notification_structure(self):
        """Verify repeat alert notification has repeat tag and priority 4."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")
        harness.place_cup(weight_g=350.0, duration_ms=2000)
        # Advance 45s + 6s
        harness.advance_time(duration_ms=52 * 1000, step_ms=500)

        notif = assert_notification_sent(harness.notifications, "Hydration Alert (Repeat)")
        assert notif["priority"] == 4
        assert "repeat" in notif["tags"]

    def test_goal_achieved_notification_structure(self):
        """Verify goal achievement notification has tada/trophy tags."""
        harness = E2ETestHarness(daily_goal_ml=500.0)
        harness.place_cup(weight_g=600.0, duration_ms=2000)
        harness.take_drink(sip_ml=500.0, duration_ms=2000)

        notif = assert_notification_sent(harness.notifications, "Daily Goal Achieved!")
        assert notif["priority"] == 3
        assert "trophy" in notif["tags"]

    def test_notification_queue_bounded_capacity(self):
        """Verify notification queue retains bounded capacity of 3 items."""
        harness = E2ETestHarness()
        # Direct stimulation of 4 sequential drinks
        harness.place_cup(weight_g=800.0, duration_ms=2000)
        harness.take_drink(sip_ml=50.0, duration_ms=2000)
        harness.take_drink(sip_ml=50.0, duration_ms=2000)
        harness.take_drink(sip_ml=50.0, duration_ms=2000)
        harness.take_drink(sip_ml=50.0, duration_ms=2000)

        # Underlying queue capacity is capped at 3
        queue = harness.adapter.device.notification_queue
        assert len(queue) == 3
