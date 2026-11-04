"""Tier 1 - Feature 11: UART Command Parser Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import E2ETestHarness, assert_weight_g


class TestT1F11UartCommands:
    """Test suite for Feature 11 UART command interface and parser."""

    def test_demo_on_and_off_commands(self):
        """Verify DEMO ON and DEMO OFF toggle mode and return OK."""
        harness = E2ETestHarness()

        # Command: DEMO ON
        resp_on = harness.send_command("DEMO ON")
        assert resp_on["status"] == "OK"
        assert resp_on["demo"] is True
        assert harness.telemetry["demo"] is True

        # Command: DEMO OFF
        resp_off = harness.send_command("DEMO OFF")
        assert resp_off["status"] == "OK"
        assert resp_off["demo"] is False
        assert harness.telemetry["demo"] is False

    def test_tare_command_zeroes_reading(self):
        """Verify TARE command sets current weight as offset."""
        harness = E2ETestHarness()
        # Feed 15.0g
        harness.tick(duration_ms=1000, step_ms=100, weight_g=15.0)
        assert_weight_g(harness.telemetry, 15.0)

        # Send TARE
        resp = harness.send_command("TARE")
        assert resp["status"] == "OK"
        assert resp["weight_g"] == 0.0
        assert_weight_g(harness.telemetry, 0.0)

    def test_reset_command_clears_consumed_volume(self):
        """Verify RESET command resets consumed_ml to 0."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.take_drink(sip_ml=120.0, duration_ms=2000)
        assert harness.telemetry["consumed_ml"] == 120.0

        resp = harness.send_command("RESET")
        assert resp["status"] == "OK"
        assert resp["consumed_ml"] == 0
        assert harness.telemetry["consumed_ml"] == 0.0

    def test_goal_command_updates_target_volume(self):
        """Verify GOAL 2500 updates daily goal and recalculates progress."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        harness.take_drink(sip_ml=250.0, duration_ms=2000)

        resp = harness.send_command("GOAL 2500")
        assert resp["status"] == "OK"
        assert resp["goal_ml"] == 2500
        # 250 / 2500 = 10.0%
        assert abs(resp["progress_pct"] - 10.0) <= 0.2
        assert harness.telemetry["goal_ml"] == 2500.0

    def test_status_command_returns_telemetry_snapshot(self):
        """Verify STATUS command returns valid telemetry snapshot packet."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=350.0, duration_ms=2000)

        resp = harness.send_command("STATUS")
        assert resp["type"] == "response"
        assert resp["state"] == "CUP_PRESENT"
        assert abs(resp["weight_g"] - 350.0) <= 0.5
        assert "consumed_ml" in resp
        assert "goal_ml" in resp
