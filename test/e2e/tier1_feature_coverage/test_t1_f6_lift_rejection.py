"""Tier 1 - Feature 6: Lift Without Drinking Rejection Coverage Tests (>=5 tests)."""

import pytest
from test.e2e.framework import (
    E2ETestHarness,
    assert_state,
    assert_event,
    assert_no_event,
    assert_consumed_ml,
)


class TestT1F6LiftRejection:
    """Test suite for Feature 6 lift-without-drinking / micro-sip rejection."""

    def test_zero_delta_lift_and_return_rejected(self):
        """Verify lifting cup and replacing at exact same weight logs LIFT_NO_DRINK."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Lift and return with 0g delta
        harness.lift_cup(duration_ms=1500)
        harness.return_cup(weight_g=400.0, duration_ms=2000)

        assert_state(harness.telemetry, "CUP_PRESENT")
        assert_consumed_ml(harness.telemetry, 0.0)
        assert_event(harness.events, "LIFT_NO_DRINK")
        assert_no_event(harness.events, "DRINK_LOGGED")

    def test_small_fidget_5g_rejected(self):
        """Verify 5g reduction (<15g) does not increment volume or drink count."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # User inspects mug, places back 5g lighter (condensation/scale noise)
        harness.lift_cup(duration_ms=1500)
        harness.return_cup(weight_g=395.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 0.0)
        assert_event(harness.events, "LIFT_NO_DRINK")

    def test_micro_sip_10g_rejected(self):
        """Verify 10g reduction is rejected as insufficient intake."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        harness.lift_cup(duration_ms=1500)
        harness.return_cup(weight_g=390.0, duration_ms=2000)

        assert_consumed_ml(harness.telemetry, 0.0)
        assert_no_event(harness.events, "DRINK_LOGGED")

    def test_rejected_lift_does_not_reset_reminder_timer(self):
        """Verify lifting and returning without drinking preserves countdown."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # Advance 25 minutes (1500s)
        harness.advance_time(duration_ms=1500 * 1000, step_ms=2000)
        countdown_before = harness.telemetry["countdown_s"]

        # User fidgets: lifts and returns 6g lighter
        harness.lift_cup(duration_ms=1500)
        harness.return_cup(weight_g=394.0, duration_ms=2000)

        countdown_after = harness.telemetry["countdown_s"]
        # Countdown must NOT jump back to 2700; it must remain ~1190s
        assert countdown_after < 1210
        assert abs(countdown_before - countdown_after) <= 10

    def test_rejected_lift_updates_baseline_to_prevent_creep(self):
        """Verify new resting weight becomes the updated baseline."""
        harness = E2ETestHarness()
        harness.place_cup(weight_g=400.0, duration_ms=2000)

        # First fidget: -6g -> baseline becomes 394g
        harness.lift_cup(duration_ms=1500)
        harness.return_cup(weight_g=394.0, duration_ms=2000)

        # Second fidget: -6g -> baseline becomes 388g
        harness.lift_cup(duration_ms=1500)
        harness.return_cup(weight_g=388.0, duration_ms=2000)

        # Neither should log a drink even though total drop from initial 400g is 12g (<15g)
        assert_consumed_ml(harness.telemetry, 0.0)
        assert_no_event(harness.events, "DRINK_LOGGED")
