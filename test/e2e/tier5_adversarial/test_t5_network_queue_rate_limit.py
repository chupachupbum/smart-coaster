"""Tier 5 - Adversarial Test 2: Network Push Queue Saturation, Drop Policy & Rate Limiting."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_consumed_ml,
    assert_notification_sent,
    assert_no_notification_sent,
    assert_event,
)
from test.e2e.framework.target_adapter import VirtualDeviceAdapter


class TestT5NetworkQueueRateLimit:
    """Adversarial stress suite for network push notifications, queue drop policy, and backoff."""

    def test_offline_queue_saturation_drop_oldest(self):
        """When offline, bursts of notifications drop oldest items to maintain bounded capacity of 3."""
        adapter = VirtualDeviceAdapter(daily_goal_ml=2000.0, default_wifi="DISCONNECTED")
        harness = E2ETestHarness(adapter=adapter)

        harness.place_cup(500.0, duration_ms=2000)

        # Generate 5 consecutive drinks while offline (sip 20, 30, 40, 50, 60 ml)
        sips = [20.0, 30.0, 40.0, 50.0, 60.0]
        for sip in sips:
            harness.take_drink(sip_ml=sip, duration_ms=2000)

        # Queue capacity is capped at 3
        queue = list(adapter.device.notification_queue)
        assert len(queue) == 3, f"Queue size should be capped at 3, got {len(queue)}"

        # Verify the 3 retained items correspond to the 3 newest drinks (40, 50, 60 ml)
        messages = [n["message"] for n in queue]
        assert any("40ml" in m for m in messages), f"40ml drink missing from queue: {messages}"
        assert any("50ml" in m for m in messages), f"50ml drink missing from queue: {messages}"
        assert any("60ml" in m for m in messages), f"60ml drink missing from queue: {messages}"

        # Verify oldest drinks (20ml, 30ml) were dropped from active queue
        assert not any("20ml" in m for m in messages), "20ml drink was not dropped on overflow!"
        assert not any("30ml" in m for m in messages), "30ml drink was not dropped on overflow!"

    def test_goal_reached_simultaneous_notifications(self):
        """A drink crossing daily goal generates both DRINK_LOGGED and GOAL_REACHED notifications."""
        harness = E2ETestHarness(daily_goal_ml=100.0)
        harness.place_cup(300.0, duration_ms=2000)

        # Drink 120ml (exceeding 100ml goal in one sip)
        harness.take_drink(sip_ml=120.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 120.0)
        assert_event(harness.events, "DRINK_LOGGED")
        assert_event(harness.events, "GOAL_REACHED")

        # Both notifications must be dispatched cleanly
        assert_notification_sent(harness.notifications, "Drink Logged - Smart Coaster", priority=3)
        assert_notification_sent(harness.notifications, "Daily Goal Achieved!", priority=3)

        # Total notifications queued = 2 (fits in queue of 3 without dropping)
        assert len(harness.notifications) == 2

    def test_repeat_alert_notification_burst(self):
        """Verify primary and repeated alerts generate correct priority 4 notifications."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")

        harness.place_cup(250.0, duration_ms=1800)

        # 46 seconds -> primary reminder
        harness.advance_time(duration_ms=46000, step_ms=1000)
        assert_notification_sent(
            harness.notifications,
            "Hydration Alert",
            priority=4,
            tag_contains="alarm_clock",
        )

        # 6 more seconds -> repeat reminder 1
        harness.advance_time(duration_ms=6000, step_ms=1000)
        assert_notification_sent(
            harness.notifications,
            "Hydration Alert (Repeat)",
            priority=4,
            tag_contains="repeat",
        )

    def test_non_drink_actions_do_not_inject_notifications(self):
        """Actions such as auto-tare, lift-without-drink, and refill do not generate push notifications."""
        harness = E2ETestHarness()

        # 1. Boot auto-tare
        harness.tick(duration_ms=5200, step_ms=100, weight_g=2.5)
        assert_event(harness.events, "AUTO_TARE")
        assert len(harness.notifications) == 0

        # 2. Cup placement
        harness.place_cup(300.0, duration_ms=2000)
        assert len(harness.notifications) == 0

        # 3. Lift without drink (-5g)
        harness.lift_cup(duration_ms=1500)
        harness.return_cup(295.0, duration_ms=2000)
        assert_event(harness.events, "LIFT_NO_DRINK")
        assert len(harness.notifications) == 0

        # 4. Refill (+100g)
        harness.refill_cup(refill_ml=100.0, duration_ms=2000)
        assert_event(harness.events, "REFILL_DETECTED")
        assert len(harness.notifications) == 0

