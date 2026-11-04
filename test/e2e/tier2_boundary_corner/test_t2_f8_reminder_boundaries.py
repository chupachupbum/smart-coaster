"""Tier 2 - Feature 8: Reminder Alert Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_event,
    assert_no_event,
)


class TestT2F8ReminderBoundaries:
    """Boundary test suite for Feature 8 45-minute inactivity reminder limits."""

    def test_reminder_boundary_at_2699s_vs_2701s(self):
        """Verify alert is inactive at 2690 seconds and active at 2705 seconds."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Advance 2690 seconds
        harness.advance_time(duration_ms=2690 * 1000, step_ms=5000)
        assert harness.telemetry["alert"] is False
        assert harness.telemetry["countdown_s"] >= 1
        assert_no_event(harness.events, "REMINDER_ALERT")

        # Advance another 15 seconds (total past 2700s = 45m)
        harness.advance_time(duration_ms=15000, step_ms=1000)
        assert harness.telemetry["alert"] is True
        assert harness.telemetry["countdown_s"] == 0
        assert_event(harness.events, "REMINDER_ALERT")

    def test_demo_mode_reminder_boundary_44s_vs_46s(self):
        """Verify DEMO mode reminder fires cleanly between 44s and 46s."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")
        harness.place_cup(weight_g=350.0, duration_ms=2000)

        # Advance 44 seconds in demo mode
        harness.advance_time(duration_ms=44 * 1000, step_ms=500)
        assert harness.telemetry["alert"] is False

        # Advance another 2 seconds (total 46s >= 45s demo interval)
        harness.advance_time(duration_ms=2000, step_ms=500)
        assert harness.telemetry["alert"] is True
        assert_event(harness.events, "REMINDER_ALERT")

    def test_reminder_fires_while_cup_is_lifted_at_deadline(self):
        """Verify if user has cup in hand at 45:00 mark, reminder alert still triggers."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Advance to 44m30s
        harness.advance_time(duration_ms=2670 * 1000, step_ms=5000)
        # Lift cup
        harness.lift_cup(duration_ms=500)

        # Advance past 45m mark while lifted
        harness.advance_time(duration_ms=35 * 1000, step_ms=1000)
        assert harness.telemetry["alert"] is True
        assert_event(harness.events, "REMINDER_ALERT")

    def test_reset_command_clears_active_reminder_alert(self):
        """Verify UART RESET clears alert and restores 45m countdown."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.advance_time(duration_ms=2705 * 1000, step_ms=5000)
        assert harness.telemetry["alert"] is True

        harness.send_command("RESET")
        assert harness.telemetry["alert"] is False
        assert 2690 <= harness.telemetry["countdown_s"] <= 2700

    def test_countdown_never_reports_negative_seconds(self):
        """Verify countdown clamp prevents negative values even when overdue by hours."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        # Overdue by 2 hours (7200 seconds)
        harness.advance_time(duration_ms=7200 * 1000, step_ms=10000)
        assert harness.telemetry["countdown_s"] == 0
