"""Tier 1 - Feature 10: Daily Intake & Goal Tracking Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_event,
    assert_consumed_ml,
    assert_notification_sent,
)


class TestT1F10IntakeGoal:
    """Test suite for Feature 10 daily intake accumulation and goal progress."""

    def test_default_daily_goal_initialized_to_2000ml(self):
        """Verify default daily target is 2000 ml."""
        harness = E2ETestHarness()
        assert harness.telemetry["goal_ml"] == 2000.0
        assert harness.telemetry["consumed_ml"] == 0.0
        assert harness.telemetry["progress_pct"] == 0.0

    def test_progress_percentage_calculation(self):
        """Verify 500ml intake computes to 25.0% progress."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=600.0, duration_ms=2000)

        # Take multiple drinks totaling 500ml
        harness.take_drink(sip_ml=250.0, duration_ms=2000)
        harness.take_drink(sip_ml=250.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 500.0)
        assert abs(harness.telemetry["progress_pct"] - 25.0) <= 0.2

    def test_reaching_daily_goal_triggers_event(self):
        """Verify reaching 2000ml fires GOAL_REACHED event."""
        harness = E2ETestHarness(daily_goal_ml=1000.0)
        harness.place_cup(weight_g=600.0, duration_ms=2000)

        # Drink 1: 500ml
        harness.take_drink(sip_ml=500.0, duration_ms=2000)
        # Refill bottle to continue
        harness.refill_cup(refill_ml=500.0, duration_ms=2000)
        # Drink 2: 500ml (total 1000ml >= goal)
        harness.take_drink(sip_ml=500.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 1000.0)
        assert_event(harness.events, "GOAL_REACHED", {"goal_ml": 1000.0})

    def test_goal_achievement_dispatches_push_notification(self):
        """Verify goal achievement sends celebratory push notification."""
        harness = E2ETestHarness(daily_goal_ml=500.0)
        harness.place_cup(weight_g=600.0, duration_ms=2000)
        harness.take_drink(sip_ml=500.0, duration_ms=2000)

        assert_notification_sent(
            harness.notifications,
            title="Daily Goal Achieved!",
            priority=3,
            tag_contains="trophy",
        )

    def test_intake_surpassing_goal_exceeds_100_percent(self):
        """Verify progress percentage cleanly exceeds 100% when goal is surpassed."""
        harness = E2ETestHarness(daily_goal_ml=500.0)
        harness.place_cup(weight_g=800.0, duration_ms=2000)
        harness.take_drink(sip_ml=600.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 600.0)
        assert abs(harness.telemetry["progress_pct"] - 120.0) <= 0.2
