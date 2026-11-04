"""Tier 2 - Feature 10: Goal Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import E2ETestHarness


class TestT2F10GoalBoundaries:
    """Boundary test suite for Feature 10 daily hydration goal boundaries."""

    def test_goal_minimum_boundary_acceptance_100ml(self):
        """Verify minimum allowable goal of 100ml is accepted."""
        harness = E2ETestHarness()
        resp = harness.send_command("GOAL 100")
        assert resp["status"] == "OK"
        assert resp["goal_ml"] == 100
        assert harness.telemetry["goal_ml"] == 100.0

    def test_goal_below_minimum_rejection_99ml(self):
        """Verify goal of 99ml (<100ml) is rejected with ERROR status."""
        harness = E2ETestHarness()
        resp = harness.send_command("GOAL 99")
        assert resp["status"] == "ERROR"
        assert harness.telemetry["goal_ml"] == 2000.0  # Remains original

    def test_goal_maximum_boundary_acceptance_10000ml(self):
        """Verify maximum allowable goal of 10,000ml is accepted."""
        harness = E2ETestHarness()
        resp = harness.send_command("GOAL 10000")
        assert resp["status"] == "OK"
        assert resp["goal_ml"] == 10000
        assert harness.telemetry["goal_ml"] == 10000.0

    def test_goal_above_maximum_rejection_10001ml(self):
        """Verify goal of 10,001ml (>10000ml) is rejected with ERROR status."""
        harness = E2ETestHarness()
        resp = harness.send_command("GOAL 10001")
        assert resp["status"] == "ERROR"
        assert harness.telemetry["goal_ml"] == 2000.0

    def test_goal_zero_and_negative_rejection(self):
        """Verify zero or non-numeric arguments return ERROR."""
        harness = E2ETestHarness()
        resp_zero = harness.send_command("GOAL 0")
        assert resp_zero["status"] == "ERROR"

        resp_invalid = harness.send_command("GOAL abc")
        assert resp_invalid["status"] == "ERROR"

    def test_goal_extreme_progress_percentage(self):
        """Verify progress percentage scales cleanly with low goal (e.g. 100ml) and 500ml intake."""
        harness = E2ETestHarness()
        harness.send_command("GOAL 100")
        harness.place_cup(weight_g=600.0, duration_ms=2000)
        harness.take_drink(sip_ml=500.0, duration_ms=2000)

        # 500 / 100 = 500.0%
        assert abs(harness.telemetry["progress_pct"] - 500.0) <= 0.5
