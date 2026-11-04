"""Tier 2 - Feature 12: Wi-Fi Push Notifications Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_notification_sent,
)


class TestT2F12PushBoundaries:
    """Boundary test suite for Feature 12 Wi-Fi HTTP push notifications queue limits."""

    def test_queue_overflow_drops_oldest_preserving_three(self):
        """Verify queue overflow drops the oldest notification to preserve memory."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=800.0, duration_ms=2000)

        # Drink 1: 50ml
        harness.take_drink(sip_ml=50.0, duration_ms=2000)
        # Drink 2: 50ml
        harness.take_drink(sip_ml=50.0, duration_ms=2000)
        # Drink 3: 50ml
        harness.take_drink(sip_ml=50.0, duration_ms=2000)
        # Drink 4: 50ml (causes drop of oldest Drink 1)
        harness.take_drink(sip_ml=50.0, duration_ms=2000)

        queue = list(harness.adapter.device.notification_queue)
        assert len(queue) == 3
        # Newest message mentions total today: 200/2000ml
        assert "200" in queue[-1]["message"]

    def test_burst_notifications_queue_saturation(self):
        """Verify rapid sequence of alerts and drinks caps queue cleanly at 3."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Trigger primary reminder + 3 repeats
        harness.advance_time(duration_ms=(45 + 16) * 1000, step_ms=500)

        queue = harness.adapter.device.notification_queue
        assert len(queue) == 3

    def test_disconnected_wifi_telemetry_reflection(self):
        """Verify telemetry reports DISCONNECTED when Wi-Fi is lost."""
        harness = E2ETestHarness()
        harness.adapter.device.wifi_status = "DISCONNECTED"

        telemetry = harness.telemetry
        assert telemetry["wifi"] == "DISCONNECTED"

    def test_notification_title_and_tags_escaping_integrity(self):
        """Verify special characters and commas in tags header are formatted cleanly."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.take_drink(sip_ml=60.0, duration_ms=2000)

        notif = assert_notification_sent(harness.notifications, "Drink Logged - Smart Coaster")
        assert notif["tags"] == "glass_of_milk,droplet"

    def test_priority_hierarchy_enforcement(self):
        """Verify reminders are priority 4 (High) while normal drinks are priority 3 (Default)."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Trigger reminder
        harness.advance_time(duration_ms=46 * 1000, step_ms=500)
        reminder_notif = assert_notification_sent(harness.notifications, "Hydration Alert")
        assert reminder_notif["priority"] == 4

        # Clear with drink
        harness.take_drink(sip_ml=50.0, duration_ms=2000)
        drink_notif = [n for n in harness.notifications if n["title"] == "Drink Logged - Smart Coaster"][-1]
        assert drink_notif["priority"] == 3
