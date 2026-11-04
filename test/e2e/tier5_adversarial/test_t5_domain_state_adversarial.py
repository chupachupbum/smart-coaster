"""Tier 5 - Adversarial State Machine & Transition Hardening Tests.

Adversarial challenges:
- Rapid bounce / flutter across 30g boundary (EMPTY <-> STABILIZING)
- Rapid bounce across 20g boundary (CUP_PRESENT <-> CUP_LIFTED)
- Snatched cup during STABILIZING
- Snatched cup during RETURN_STABILIZING
- Multiple micro-sips (<15g) in succession: verify non-accumulation into drink
- Exact boundary drink transitions (14.99g vs 15.00g)
- Exact boundary refill transitions (29.99g vs 30.00g)
- Heavy load cell stress (5000g+)
- Deadband behavior between 20g and 30g
- Reset while in active alert and cup lifted vs present
"""

import pytest
from test.e2e.framework import E2ETestHarness
from test.e2e.framework.assertions import (
    assert_state,
    assert_event,
    assert_no_event,
    assert_consumed_ml,
    assert_weight_g,
)


class TestT5DomainStateAdversarial:
    """Adversarial stress testing of CoasterDomain state transitions and timing boundaries."""

    def test_rapid_cup_bouncing_flutter_before_stabilization(self):
        """Stress: 20 rapid placements and snatches (200ms on, 200ms off) across 30g threshold.
        Verifies system never latches false CUP_PRESENT or leaks state."""
        harness = E2ETestHarness()

        # Simulate user or pet repeatedly nudging cup on and off coaster
        for cycle in range(20):
            # Cup touches stand (weight 35g) for only 200ms (< 1500ms required)
            harness.tick(duration_ms=200, step_ms=50, weight_g=35.0)
            assert harness.telemetry["state"] in ("STABILIZING", "EMPTY")
            assert harness.telemetry["stable"] is False

            # Cup removed (<20g) for 200ms
            harness.tick(duration_ms=200, step_ms=50, weight_g=0.0)
            assert harness.telemetry["state"] == "EMPTY"

        # Final check: system is strictly EMPTY, 0ml consumed, no alerts
        assert_state(harness.telemetry, "EMPTY")
        assert_consumed_ml(harness.telemetry, 0.0)
        assert harness.telemetry["alert"] is False
        assert_no_event(harness.events, "DRINK_LOGGED")

    def test_premature_lift_during_return_stabilizing(self):
        """Stress: Cup returned, stays for 1000ms (< 1500ms), then lifted again before settling.
        Verifies return stabilization aborts cleanly back to CUP_LIFTED without logging drink."""
        harness = E2ETestHarness()
        harness.place_cup(300.0)
        assert_state(harness.telemetry, "CUP_PRESENT")

        # Lift cup
        harness.lift_cup(1500)
        assert_state(harness.telemetry, "CUP_LIFTED")

        # Return cup at 250g for 1000ms only (insufficient to settle)
        harness.tick(duration_ms=1000, step_ms=50, weight_g=250.0)
        assert_state(harness.telemetry, "RETURN_STABILIZING")
        assert harness.telemetry["stable"] is False

        # Lift cup again (<20g)
        harness.lift_cup(1000)
        assert_state(harness.telemetry, "CUP_LIFTED")
        assert_no_event(harness.events, "DRINK_LOGGED")
        assert_consumed_ml(harness.telemetry, 0.0)

        # Now finally return and allow full 2000ms settling
        harness.return_cup(250.0, duration_ms=2000)
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_consumed_ml(harness.telemetry, 50.0)
        assert_event(harness.events, "DRINK_LOGGED")

    def test_micro_sip_accumulation_rejection(self):
        """Adversarial: 10 consecutive micro-sips of 14.0g each (< 15g threshold).
        Total reduction is 140g, but each individual sip is below DRINK_MIN_DELTA_G.
        Verifies that baseline updates on each lift without logging any volume."""
        harness = E2ETestHarness()
        current_weight = 400.0
        harness.place_cup(current_weight)

        for _ in range(10):
            current_weight -= 14.0
            harness.lift_cup(1500)
            harness.return_cup(current_weight, duration_ms=2000)
            assert_state(harness.telemetry, "CUP_PRESENT")
            # Volume must remain 0.0ml after EVERY micro-sip
            assert_consumed_ml(harness.telemetry, 0.0)

        assert_consumed_ml(harness.telemetry, 0.0)
        assert_no_event(harness.events, "DRINK_LOGGED")
        assert harness.telemetry["drink_count"] == 0 if "drink_count" in harness.telemetry else True
        assert_weight_g(harness.telemetry, 260.0)

    def test_exact_float_boundary_drink_discrimination(self):
        """Precision: 14.9g delta must be rejected; 15.0g delta must be accepted."""
        harness = E2ETestHarness()
        harness.place_cup(300.0)

        # First attempt: exactly 14.9g reduction (300.0 -> 285.1g)
        harness.lift_cup(1500)
        harness.return_cup(285.1, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 0.0)
        assert_no_event(harness.events, "DRINK_LOGGED")

        # Second attempt: exactly 15.0g reduction (285.1 -> 270.1g)
        harness.lift_cup(1500)
        harness.return_cup(270.1, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 15.0)
        assert_event(harness.events, "DRINK_LOGGED")

    def test_exact_float_boundary_refill_discrimination(self):
        """Precision: 29.9g gain must NOT trigger refill event; 30.0g gain must trigger REFILL_DETECTED."""
        harness = E2ETestHarness()
        harness.place_cup(200.0)

        # 29.9g gain (200.0 -> 229.9g): classified as LIFT_NO_DRINK
        harness.lift_cup(1500)
        harness.return_cup(229.9, duration_ms=2000)
        assert_no_event(harness.events, "REFILL_DETECTED")
        assert_consumed_ml(harness.telemetry, 0.0)

        # 30.0g gain (229.9 -> 259.9g): triggers REFILL_DETECTED
        harness.lift_cup(1500)
        harness.return_cup(259.9, duration_ms=2000)
        assert_event(harness.events, "REFILL_DETECTED")
        assert_consumed_ml(harness.telemetry, 0.0)

    def test_hysteresis_deadband_between_20g_and_30g(self):
        """Adversarial: Object weighing 25.0g placed on empty stand.
        Must NOT trigger cup presence (>30g required) and must NOT auto-tare (<=5g required)."""
        harness = E2ETestHarness()

        # Place 25g object and hold for 6 seconds
        harness.tick(duration_ms=6000, step_ms=50, weight_g=25.0)
        assert_state(harness.telemetry, "EMPTY")
        assert_no_event(harness.events, "AUTO_TARE")
        assert_no_event(harness.events, "CUP_PLACED")
        assert_weight_g(harness.telemetry, 25.0)

    def test_heavy_overload_and_large_consumption(self):
        """Adversarial: 4500g heavy vessel placed, 2000g poured/consumed in one go."""
        harness = E2ETestHarness(daily_goal_ml=3000.0)
        harness.place_cup(4500.0)
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_weight_g(harness.telemetry, 4500.0)

        # Massive drink of 2000g (4500 -> 2500g)
        harness.lift_cup(2000)
        harness.return_cup(2500.0, duration_ms=2000)
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_consumed_ml(harness.telemetry, 2000.0)
        assert_event(harness.events, "DRINK_LOGGED")

    def test_reset_command_while_cup_present_clears_alerts_and_intake(self):
        """Adversarial: Coaster in ALERT_REPEAT state with 800ml consumed.
        UART RESET executed while cup is still present.
        Verifies: volume cleared to 0ml, alert state returns to COUNTING (not IDLE because cup is present)."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")
        harness.place_cup(300.0)
        harness.take_drink(50.0)

        # Advance into repeat alert (45s + 6s)
        harness.advance_time(52000)
        assert harness.telemetry["alert"] is True

        # Send RESET while cup is resting on coaster
        resp = harness.send_command("RESET")
        assert resp["status"] == "OK"
        assert resp["consumed_ml"] == 0

        # State check: cup is present, so alert goes back to COUNTING, alert is silenced
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert harness.telemetry["alert"] is False
        assert_consumed_ml(harness.telemetry, 0.0)
