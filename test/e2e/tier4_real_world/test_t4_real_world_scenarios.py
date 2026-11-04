"""Tier 4: Real-World Hydration Application Scenarios (>=5 full user scenarios)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_event,
    assert_no_event,
    assert_consumed_ml,
    assert_weight_g,
    assert_notification_sent,
)


class TestT4RealWorldScenarios:
    """Comprehensive end-to-end user hydration workflows."""

    def test_scenario_1_standard_workday_hydration_to_goal(self):
        """Scenario 1: Standard 8-Hour Workday Hydration.
        
        Lifecycle:
        - 08:30: Coaster boots, auto-tares zero drift.
        - 08:31: User places 800g mug on coaster.
        - 09:15: First drink of 250ml.
        - 10:30: Second drink of 250ml (mug is empty, 300g remaining).
        - 10:35: User refills mug with 500ml water (weight becomes 800g).
        - 11:30: Third drink of 300ml.
        - 13:30: Fourth drink of 200ml (refill +500ml).
        - 15:00: Fifth drink of 400ml.
        - 16:30: Sixth drink of 400ml -> reaches 2000ml goal!
        - Evening: Verifies goal reached notification, 100% progress, clean state.
        """
        harness = E2ETestHarness(daily_goal_ml=2000.0)

        # 08:30: Startup and auto-tare +2.0g drift
        harness.tick(duration_ms=5100, step_ms=50, weight_g=2.0)
        assert_event(harness.events, "AUTO_TARE")
        assert_weight_g(harness.telemetry, 0.0)

        # 08:31: User places 800g mug
        harness.place_cup(weight_g=802.0, duration_ms=2000)
        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_weight_g(harness.telemetry, 800.0)

        # 09:15: Drink 1 - 250ml
        harness.advance_time(duration_ms=44 * 60 * 1000, step_ms=5000)
        harness.take_drink(sip_ml=250.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 250.0)

        # 10:30: Drink 2 - 250ml (total 500ml)
        harness.advance_time(duration_ms=40 * 60 * 1000, step_ms=5000)
        harness.take_drink(sip_ml=250.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 500.0)

        # 10:35: Refill +500ml water
        harness.advance_time(duration_ms=5 * 60 * 1000, step_ms=5000)
        harness.refill_cup(refill_ml=500.0, duration_ms=2000)
        assert_event(harness.events, "REFILL_DETECTED")
        assert_consumed_ml(harness.telemetry, 500.0)

        # 11:30: Drink 3 - 300ml (total 800ml)
        harness.advance_time(duration_ms=40 * 60 * 1000, step_ms=5000)
        harness.take_drink(sip_ml=300.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 800.0)

        # 13:30: Drink 4 - 200ml (total 1000ml) and Refill +500ml
        harness.advance_time(duration_ms=40 * 60 * 1000, step_ms=5000)
        harness.take_drink(sip_ml=200.0, duration_ms=2000)
        harness.refill_cup(refill_ml=500.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 1000.0)

        # 15:00: Drink 5 - 500ml (total 1500ml) and Refill +500ml
        harness.advance_time(duration_ms=40 * 60 * 1000, step_ms=5000)
        harness.take_drink(sip_ml=500.0, duration_ms=2000)
        harness.refill_cup(refill_ml=500.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 1500.0)

        # 16:30: Drink 6 - 500ml (total 2000ml -> reaches goal!)
        harness.advance_time(duration_ms=40 * 60 * 1000, step_ms=5000)
        harness.take_drink(sip_ml=500.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 2000.0)
        assert harness.telemetry["progress_pct"] == 100.0
        assert_event(harness.events, "GOAL_REACHED", {"goal_ml": 2000.0})
        assert_notification_sent(harness.notifications, "Daily Goal Achieved!")

    def test_scenario_2_tea_refill_and_intermittent_sipping(self):
        """Scenario 2: Tea Refill Followed by Intermittent Sips.
        
        Lifecycle:
        - Place 350g tea mug (150g mug + 200g water).
        - Take three small sips of 40ml each (total 120ml).
        - Steep second cup: refill with 350g hot water (increases to 580g).
        - Sips 50g, 50g, 60g from the refilled tea.
        - Verifies accurate accumulation without double-counting refill.
        """
        harness = E2ETestHarness()
        harness.place_cup(weight_g=350.0, duration_ms=2000)

        # Three 40ml sips
        harness.take_drink(sip_ml=40.0, duration_ms=2000)
        harness.advance_time(duration_ms=10 * 60 * 1000, step_ms=5000)
        harness.take_drink(sip_ml=40.0, duration_ms=2000)
        harness.advance_time(duration_ms=10 * 60 * 1000, step_ms=5000)
        harness.take_drink(sip_ml=40.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 120.0)

        # Refill with 350g hot water
        harness.refill_cup(refill_ml=350.0, duration_ms=2000)
        assert_event(harness.events, "REFILL_DETECTED", {"refill_delta_ml": 350.0})
        assert_consumed_ml(harness.telemetry, 120.0)

        # Sip 1: 50ml -> 170ml
        harness.take_drink(sip_ml=50.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 170.0)

        # Sip 2: 50ml -> 220ml
        harness.take_drink(sip_ml=50.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 220.0)

        # Sip 3: 60ml -> 280ml
        harness.take_drink(sip_ml=60.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 280.0)

    def test_scenario_3_accidental_bump_and_fidget_jiggle_rejection(self):
        """Scenario 3: Accidental Bump, Desk Vibration & Fidget Jiggle Rejection.
        
        Lifecycle:
        - Coaster idle. Heavy foot traffic causes +/-3g spikes (no false presence).
        - User places 400g glass.
        - Desk bumps produce 5g vibrations; filter prevents premature state changes.
        - User inspects glass, picks up for 3 seconds, replaces without drinking (-3g condensation).
        - Verifies volume remains 0.0ml, drink count remains 0.
        - User takes actual 120ml drink -> confirmed and logged.
        """
        harness = E2ETestHarness()

        # Phase 1: Heavy bumps on empty desk (spikes to 15g, drops back)
        for _ in range(5):
            harness.tick(duration_ms=100, step_ms=50, weight_g=15.0)
            harness.tick(duration_ms=500, step_ms=50, weight_g=0.0)
        assert_state(harness.telemetry, "EMPTY")

        # Phase 2: Place glass
        harness.place_cup(weight_g=400.0, duration_ms=2000)
        assert_state(harness.telemetry, "CUP_PRESENT")

        # Phase 3: Desk vibrations while glass sits
        for _ in range(10):
            harness.tick(duration_ms=50, step_ms=50, weight_g=398.0)
            harness.tick(duration_ms=50, step_ms=50, weight_g=403.0)
        assert_state(harness.telemetry, "CUP_PRESENT")

        # Phase 4: Fidget lift and return with negligible delta
        harness.lift_cup(duration_ms=3000)
        harness.return_cup(weight_g=397.0, duration_ms=2000)
        assert_event(harness.events, "LIFT_NO_DRINK")
        assert_consumed_ml(harness.telemetry, 0.0)

        # Phase 5: Legitimate drink of 120ml
        harness.take_drink(sip_ml=120.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 120.0)
        assert_event(harness.events, "DRINK_LOGGED", {"drink_delta_ml": 120.0})

    def test_scenario_4_persistent_inattention_and_alert_escalation(self):
        """Scenario 4: Persistent Inattention & Hydration Alert Escalation.
        
        Lifecycle:
        - 09:00: Place 400g cup.
        - 09:45 (45 min): Primary REMINDER_ALERT triggers. LED breathes, push notification sent.
        - 09:50 (5 min later): First REPEAT_ALERT triggers.
        - 09:55 (5 min later): Second REPEAT_ALERT triggers.
        - 09:58: User finally takes 180ml drink.
        - All alerts clear, countdown resets to 45m, LED breathing stops.
        """
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # 45 minutes of inattention -> Primary alert
        harness.advance_time(duration_ms=2705 * 1000, step_ms=5000)
        assert harness.telemetry["alert"] is True
        assert harness.led["is_alert"] is True
        assert_event(harness.events, "REMINDER_ALERT")
        assert_notification_sent(harness.notifications, "Hydration Alert", priority=4)

        # 5 minutes later -> Repeat 1
        harness.advance_time(duration_ms=305 * 1000, step_ms=5000)
        assert_event(harness.events, "REPEAT_ALERT")

        # 5 minutes later -> Repeat 2
        harness.advance_time(duration_ms=305 * 1000, step_ms=5000)
        repeats = [e for e in harness.events if e.get("event") == "REPEAT_ALERT"]
        assert len(repeats) >= 2

        # User responds: drinks 180ml
        harness.take_drink(sip_ml=180.0, duration_ms=2000)
        assert harness.telemetry["alert"] is False
        assert harness.led["is_alert"] is False
        assert_consumed_ml(harness.telemetry, 180.0)
        assert 2690 <= harness.telemetry["countdown_s"] <= 2700

    def test_scenario_5_evening_reset_and_next_day_goal_customization(self):
        """Scenario 5: Evening Session Reset & Next-Day Goal Customization.
        
        Lifecycle:
        - End-of-day: User logs into serial console and queries STATUS.
        - User sends RESET command: daily intake cleared, drink count 0.
        - User configures athlete goal: GOAL 3000.
        - Next morning: Coaster wakes up, user places 1000ml bottle.
        - User takes 300ml drink: progress calculates against 3000ml (10.0%).
        """
        harness = E2ETestHarness()
        harness.place_cup(weight_g=500.0, duration_ms=2000)
        harness.take_drink(sip_ml=250.0, duration_ms=2000)
        assert harness.telemetry["consumed_ml"] == 250.0

        # End of day reset and remove cup from coaster
        resp_reset = harness.send_command("RESET")
        assert resp_reset["status"] == "OK"
        assert harness.telemetry["consumed_ml"] == 0.0
        harness.lift_cup(duration_ms=2000)

        # Configure new goal of 3000ml for tomorrow
        resp_goal = harness.send_command("GOAL 3000")
        assert resp_goal["status"] == "OK"
        assert resp_goal["goal_ml"] == 3000
        assert harness.telemetry["goal_ml"] == 3000.0

        # Next morning: user places fresh 1000g water bottle
        harness.place_cup(weight_g=1000.0, duration_ms=2000)
        assert_state(harness.telemetry, "CUP_PRESENT")

        # Morning drink: 300ml
        harness.take_drink(sip_ml=300.0, duration_ms=2000)
        assert_consumed_ml(harness.telemetry, 300.0)
        # 300 / 3000 = 10.0%
        assert abs(harness.telemetry["progress_pct"] - 10.0) <= 0.2
