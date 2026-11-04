"""Tier 5 - UART CLI Interface Fuzzing and Stress Hardening Tests.

Adversarial challenges:
- Malformed syntax, gibberish strings, binary characters
- Goal threshold boundary extremes: 99, 100, 10000, 10001, negative, NaN, Inf
- Excessive whitespace, mixed case, and formatting resilience
- Oversized payload stress (1000+ characters)
- Rapid consecutive command bursts
- Schema conformance of STATUS response
"""

import pytest
from test.e2e.framework import E2ETestHarness


class TestT5UartCliFuzz:
    """Adversarial fuzzing and robustness validation of UART command parser."""

    def test_goal_strict_boundary_enforcement(self):
        """Precision: GOAL limits are strictly [100 .. 10000 ml].
        99 is rejected, 100 is accepted, 10000 is accepted, 10001 is rejected."""
        harness = E2ETestHarness()

        # 99 ml: Rejected
        resp_99 = harness.send_command("GOAL 99")
        assert resp_99["status"] == "ERROR"

        # 100 ml: Accepted (lower boundary)
        resp_100 = harness.send_command("GOAL 100")
        assert resp_100["status"] == "OK"
        assert resp_100["goal_ml"] == 100

        # 10000 ml: Accepted (upper boundary)
        resp_10000 = harness.send_command("GOAL 10000")
        assert resp_10000["status"] == "OK"
        assert resp_10000["goal_ml"] == 10000

        # 10001 ml: Rejected
        resp_10001 = harness.send_command("GOAL 10001")
        assert resp_10001["status"] == "ERROR"

    def test_goal_invalid_numeric_formats(self):
        """Fuzzing: GOAL with negative numbers, floats, NaN, Inf, and non-numeric junk."""
        harness = E2ETestHarness()

        for invalid_goal in ["GOAL -500", "GOAL 0", "GOAL abc", "GOAL NaN", "GOAL INF", "GOAL 500ml", "GOAL"]:
            resp = harness.send_command(invalid_goal)
            assert resp["status"] == "ERROR", f"Expected ERROR for '{invalid_goal}', got: {resp}"

    def test_whitespace_and_tabs_fuzzing(self):
        """Robustness: Commands with leading, trailing, and internal multiple spaces and tabs."""
        harness = E2ETestHarness()

        # Tab-separated GOAL
        resp1 = harness.send_command("\t\tGOAL\t  2500  \t\r\n")
        assert resp1["status"] == "OK"
        assert resp1["goal_ml"] == 2500

        # Excessively spaced DEMO ON
        resp2 = harness.send_command("    DEMO    ON    ")
        # May be parsed as OK or unrecognized depending on internal tokenization
        # Must return valid structured JSON in either case without crashing
        assert "status" in resp2

        # Mixed case with extra whitespace
        resp3 = harness.send_command("   sTaTuS   ")
        assert resp3["type"] == "response" or resp3["status"] in ("OK", "ERROR")

    def test_massive_payload_stress_fuzz(self):
        """Stress: 1500-character payload of repeated command tokens and symbols.
        Verifies parser drops or safely handles line buffer overflow without memory corruption."""
        harness = E2ETestHarness()

        huge_payload = "STATUS_TARE_RESET_GOAL_" * 70
        resp = harness.send_command(huge_payload)
        assert "status" in resp or "type" in resp
        assert resp["status"] == "ERROR"

    def test_rapid_command_burst(self):
        """Concurrency: 30 consecutive alternating commands executed without delay."""
        harness = E2ETestHarness()

        commands = ["STATUS", "TARE", "DEMO ON", "DEMO OFF", "RESET", "GOAL 2200"]
        for i in range(30):
            cmd = commands[i % len(commands)]
            resp = harness.send_command(cmd)
            assert "status" in resp or "type" in resp

        # Verify device state remains coherent
        assert harness.telemetry["goal_ml"] == 2200
        assert harness.telemetry["consumed_ml"] == 0

    def test_status_response_complete_schema_conformance(self):
        """Conformance: STATUS command must return all 11 mandated schema fields."""
        harness = E2ETestHarness()
        harness.place_cup(350.0)

        resp = harness.send_command("STATUS")

        mandated_fields = [
            "type", "ts", "state", "weight_g", "stable_g", "stable",
            "consumed_ml", "goal_ml", "progress_pct", "countdown_s", "alert", "demo"
        ]
        for field in mandated_fields:
            assert field in resp, f"Mandated field '{field}' missing from STATUS response: {resp}"

        assert resp["type"] == "response"
        assert resp["state"] == "CUP_PRESENT"
        assert resp["stable"] is True
