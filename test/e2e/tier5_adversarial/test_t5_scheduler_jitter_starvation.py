"""Tier 5 - Adversarial Test 1: Scheduler Multi-Rate Jitter & Task Starvation Prevention."""

import random
import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_weight_g,
    assert_consumed_ml,
    assert_event,
    assert_no_event,
)


class TestT5SchedulerJitterStarvation:
    """Adversarial stress suite verifying scheduler timing jitter and starvation prevention."""

    def test_timing_jitter_variable_step_sizes(self):
        """Verify stability filter and state machine tolerate non-uniform time step jitter."""
        harness = E2ETestHarness()

        # Step jitter profile simulating cooperative scheduling delay variations
        jitter_steps = [10, 35, 75, 20, 90, 15, 60, 40, 110, 25]

        # Place 250g cup with jittered clock stepping
        current_weight = 250.0
        elapsed = 0
        step_idx = 0
        while elapsed < 2200:
            step = jitter_steps[step_idx % len(jitter_steps)]
            step_idx += 1
            harness.tick(duration_ms=step, step_ms=step, weight_g=current_weight)
            elapsed += step

        # Must cleanly achieve stability and transition to CUP_PRESENT despite jitter
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_weight_g(harness.telemetry, 250.0)
        assert harness.telemetry["stable"] is True
        assert_event(harness.events, "STATE_CHANGE", {"new_state": "CUP_PRESENT"})

    def test_rapid_contact_chatter_rejection(self):
        """Simulate physical vibration / contact bounce around 20g/30g threshold."""
        harness = E2ETestHarness()

        # 40 rapid oscillations crossing between 10g and 35g at 20ms steps (50Hz chatter)
        for i in range(40):
            chatter_weight = 35.0 if (i % 2 == 0) else 10.0
            harness.tick(duration_ms=20, step_ms=20, weight_g=chatter_weight)

        # Coaster must NEVER stabilize during chatter
        assert harness.telemetry["state"] in ("EMPTY", "STABILIZING")
        assert harness.telemetry["stable"] is False
        assert_no_event(harness.events, "CUP_PLACED")

        # Now weight settles firmly to 200g for 2.0s
        harness.tick(duration_ms=2000, step_ms=50, weight_g=200.0)
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert harness.telemetry["stable"] is True
        assert_event(harness.events, "STATE_CHANGE", {"new_state": "CUP_PRESENT"})

    def test_continuous_uart_command_bombardment_during_drink_cycle(self):
        """Fire rapid UART CLI commands during active cup lift & drink cycle."""
        harness = E2ETestHarness()
        harness.place_cup(300.0, duration_ms=2000)
        assert_state(harness.telemetry, "CUP_PRESENT")

        commands = ["STATUS", "DEMO ON", "TARE", "DEMO OFF", "STATUS", "GOAL 2500"]

        # Lift cup while bombarding with commands
        harness.lift_cup(duration_ms=500)
        for cmd in commands:
            resp = harness.send_command(cmd)
            assert "status" in resp or "type" in resp

        harness.lift_cup(duration_ms=1000)
        for cmd in commands:
            resp = harness.send_command(cmd)
            assert "status" in resp or "type" in resp

        # Replace cup 50g lighter (250g) while still processing commands
        harness.return_cup(250.0, duration_ms=2000)
        for cmd in commands:
            resp = harness.send_command(cmd)
            assert "status" in resp or "type" in resp

        # State machine must correctly log the drink despite CLI bombardment
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_consumed_ml(harness.telemetry, 50.0)
        assert_event(harness.events, "DRINK_LOGGED", {"drink_delta_ml": 50.0})

    def test_micro_sips_baseline_preservation(self):
        """Series of 5 sub-threshold micro-sips (12g each < 15g threshold)."""
        harness = E2ETestHarness()
        harness.place_cup(300.0, duration_ms=2000)

        # 5 separate lifts of 12g each: 300g -> 288g -> 276g -> 264g -> 252g -> 240g
        current_w = 300.0
        for _ in range(5):
            current_w -= 12.0
            harness.lift_cup(duration_ms=1500)
            harness.return_cup(current_w, duration_ms=2000)
            assert_state(harness.telemetry, "CUP_PRESENT")
            # None of these should count as drinks!
            assert_consumed_ml(harness.telemetry, 0.0)

        assert_no_event(harness.events, "DRINK_LOGGED")

        # Now take a single 20g drink: 240g -> 220g (delta >= 15g)
        harness.lift_cup(duration_ms=1500)
        harness.return_cup(220.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 20.0)
        assert_event(harness.events, "DRINK_LOGGED", {"drink_delta_ml": 20.0})

    def test_task_starvation_prevention_long_session_soak(self):
        """Simulate long multi-hour session soak with continuous interleaved operations."""
        harness = E2ETestHarness()
        harness.send_command("DEMO ON")  # Accelerate timers: 45s reminder, 5s repeat

        # Step 1: Place 350g cup
        harness.place_cup(350.0, duration_ms=1800)
        assert_state(harness.telemetry, "CUP_PRESENT")

        # Step 2: Inactivity leads to reminder alert at 45s
        harness.advance_time(duration_ms=46000, step_ms=1000)
        assert harness.telemetry["alert"] is True
        assert_event(harness.events, "REMINDER_ALERT")

        # Step 3: Two repeat alerts fire at 5s and 10s
        harness.advance_time(duration_ms=11000, step_ms=1000)
        repeat_events = [e for e in harness.events if e.get("event") == "REPEAT_ALERT"]
        assert len(repeat_events) >= 2

        # Step 4: Drink 60ml clears alert and updates consumed total
        harness.take_drink(sip_ml=60.0, duration_ms=2000)
        assert harness.telemetry["alert"] is False
        assert_consumed_ml(harness.telemetry, 60.0)

        # Step 5: Refill +100ml
        harness.refill_cup(refill_ml=100.0, duration_ms=2000)
        assert_event(harness.events, "REFILL_DETECTED", {"refill_delta_ml": 100.0})
        assert_consumed_ml(harness.telemetry, 60.0)
