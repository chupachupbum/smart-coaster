"""Tier 2 - Feature 11: UART Command Parser Boundary & Corner Case Tests (>=5 tests)."""

import pytest
from test.e2e.framework import E2ETestHarness


class TestT2F11UartBoundaries:
    """Boundary test suite for Feature 11 UART command protocol parsing."""

    def test_uart_case_insensitivity(self):
        """Verify command parser accepts lowercase and mixed case commands."""
        harness = E2ETestHarness()

        resp1 = harness.send_command("demo on")
        assert resp1["status"] == "OK"
        assert resp1["demo"] is True

        resp2 = harness.send_command("Tare")
        assert resp2["status"] == "OK"

        resp3 = harness.send_command("reSeT")
        assert resp3["status"] == "OK"

    def test_uart_leading_and_trailing_whitespace(self):
        """Verify extra spaces and newlines around commands are stripped."""
        harness = E2ETestHarness()
        resp = harness.send_command("   STATUS   \r\n")
        assert resp["type"] == "response"
        assert "state" in resp

    def test_uart_unknown_command_returns_structured_error(self):
        """Verify unknown command returns status ERROR and list of supported commands."""
        harness = E2ETestHarness()
        resp = harness.send_command("CALIBRATE_NOW")
        assert resp["status"] == "ERROR"
        assert resp["command"] == "CALIBRATE_NOW"
        assert "Supported" in resp["msg"]

    def test_uart_empty_command_handled(self):
        """Verify empty string or whitespace-only command returns structured error."""
        harness = E2ETestHarness()
        resp = harness.send_command("    ")
        assert resp["status"] == "ERROR"

    def test_uart_oversized_payload_stress(self):
        """Verify receiving long 500-character gibberish line does not panic parser."""
        harness = E2ETestHarness()
        long_line = "FOOBAR_" * 70
        resp = harness.send_command(long_line)
        assert resp["status"] == "ERROR"
        assert resp["command"] == long_line
