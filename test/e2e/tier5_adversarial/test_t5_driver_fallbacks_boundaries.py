"""Tier 5 - Adversarial Test 3: Hardware Driver Fallbacks, Boundaries & Command Injection."""

import json
import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_weight_g,
    assert_consumed_ml,
    assert_event,
)


class TestT5DriverFallbacksBoundaries:
    """Adversarial stress suite for hardware driver fallbacks, boundary values, and injection."""

    def test_goal_command_boundary_fuzzing(self):
        """Test exact mathematical boundary limits for GOAL command (100ml .. 10000ml)."""
        harness = E2ETestHarness()

        # Underflow boundary (99.9ml)
        resp = harness.send_command("GOAL 99.9")
        assert resp.get("status") == "ERROR"

        # Valid lower boundary (100.0ml)
        resp = harness.send_command("GOAL 100")
        assert resp.get("status") == "OK"
        assert resp.get("goal_ml") == 100.0

        # Valid upper boundary (10000.0ml)
        resp = harness.send_command("GOAL 10000")
        assert resp.get("status") == "OK"
        assert resp.get("goal_ml") == 10000.0

        # Overflow boundary (10000.1ml)
        resp = harness.send_command("GOAL 10000.1")
        assert resp.get("status") == "ERROR"

        # Negative and zero
        assert harness.send_command("GOAL -500").get("status") == "ERROR"
        assert harness.send_command("GOAL 0").get("status") == "ERROR"

        # Non-numeric string
        assert harness.send_command("GOAL invalid").get("status") == "ERROR"

    def test_command_injection_and_sanitization(self):
        """Verify command parser sanitizes special characters and quotes without JSON syntax breakage."""
        harness = E2ETestHarness()

        malicious_inputs = [
            '{"hack": true}',
            'TARE "quoted"',
            'GOAL \\x00\\xFF',
            '   STATUS   ',
            'A' * 200,  # Overlong string
            'UNKNOWN_COMMAND_XYZ_123',
            'TARE\r\nRESET',
        ]

        for payload in malicious_inputs:
            resp = harness.send_command(payload)
            # Response must be a valid parsed dictionary (meaning valid JSON was emitted)
            assert isinstance(resp, dict)
            assert "status" in resp or "type" in resp

    def test_status_led_visual_patterns(self):
        """Verify status LED states across all lifecycle stages."""
        harness = E2ETestHarness()

        # Stage 1: Empty -> LED OFF
        assert harness.led["mode"] == "OFF"
        assert harness.led["duty"] == 0

        # Stage 2: Settling -> LED BLINK
        harness.tick(duration_ms=200, step_ms=50, weight_g=200.0)
        assert harness.led["mode"] == "BLINK"

        # Stage 3: Stable cup present -> LED STEADY_LOW (ambient glow)
        harness.tick(duration_ms=1800, step_ms=50, weight_g=200.0)
        assert harness.led["mode"] == "STEADY_LOW"
        assert harness.led["duty"] == 8

        # Stage 4: Return cup -> back to STEADY_LOW
        harness.lift_cup(duration_ms=1000)
        harness.return_cup(200.0, duration_ms=2000)
        assert harness.led["mode"] == "STEADY_LOW"

        # Stage 5: Hydration alert -> LED BREATHING
        harness.send_command("DEMO ON")
        harness.advance_time(duration_ms=46000, step_ms=1000)
        assert harness.led["mode"] == "BREATHING"
        assert harness.led["is_alert"] is True

    def test_cup_presence_and_lift_hysteresis_boundaries(self):
        """Test hysteresis between placement (>30g) and lift (<20g)."""
        harness = E2ETestHarness()

        # Net weight 30.0g does not trigger presence (boundary: > 30.0g required)
        harness.tick(duration_ms=2000, step_ms=100, weight_g=30.0)
        assert_state(harness.telemetry, "EMPTY")

        # Net weight 30.5g triggers placement
        harness.tick(duration_ms=2000, step_ms=100, weight_g=30.5)
        assert_state(harness.telemetry, "CUP_PRESENT")

        # Net weight drops to 20.0g: does NOT trigger lift (boundary: < 20.0g required)
        harness.tick(duration_ms=2000, step_ms=100, weight_g=20.0)
        assert_state(harness.telemetry, "CUP_PRESENT")

        # Net weight drops to 19.5g: triggers CUP_LIFTED
        harness.tick(duration_ms=1000, step_ms=100, weight_g=19.5)
        assert_state(harness.telemetry, "CUP_LIFTED")
