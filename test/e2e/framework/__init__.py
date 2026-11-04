"""Framework components for Smart Cup Stand E2E testing."""
from .virtual_device import VirtualCoasterDevice
from .target_adapter import TargetAdapter, VirtualDeviceAdapter
from .assertions import (
    assert_state,
    assert_event,
    assert_no_event,
    assert_consumed_ml,
    assert_weight_g,
    assert_notification_sent,
    assert_no_notification_sent,
)
from .harness import E2ETestHarness

__all__ = [
    "VirtualCoasterDevice",
    "TargetAdapter",
    "VirtualDeviceAdapter",
    "E2ETestHarness",
    "assert_state",
    "assert_event",
    "assert_no_event",
    "assert_consumed_ml",
    "assert_weight_g",
    "assert_notification_sent",
    "assert_no_notification_sent",
]
