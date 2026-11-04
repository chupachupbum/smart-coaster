"""Domain and Telemetry Assertions for E2E Tests."""

from typing import Dict, List, Any, Optional


def assert_state(telemetry: Dict[str, Any], expected_state: str) -> None:
    """Assert device is in expected state."""
    actual_state = telemetry.get("state")
    assert (
        actual_state == expected_state
    ), f"State mismatch: expected '{expected_state}', got '{actual_state}'"


def assert_event(
    events: List[Dict[str, Any]],
    event_name: str,
    event_data_subset: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    """Assert that a specific domain event was emitted, matching optional data subset."""
    matching = [e for e in events if e.get("event") == event_name]
    assert len(matching) > 0, f"Expected event '{event_name}' was not emitted. Emitted: {[e.get('event') for e in events]}"

    if event_data_subset:
        found = False
        for evt in matching:
            data = evt.get("event_data", {})
            if all(data.get(k) == v for k, v in event_data_subset.items()):
                found = True
                return evt
        assert found, f"Event '{event_name}' found, but data did not match {event_data_subset}. Actual: {[e.get('event_data') for e in matching]}"

    return matching[-1]


def assert_no_event(events: List[Dict[str, Any]], event_name: str) -> None:
    """Assert that a domain event was NOT emitted."""
    matching = [e for e in events if e.get("event") == event_name]
    assert len(matching) == 0, f"Event '{event_name}' was unexpectedly emitted {len(matching)} time(s)."


def assert_consumed_ml(telemetry: Dict[str, Any], expected_ml: float, tolerance: float = 0.5) -> None:
    """Assert consumed intake volume in milliliters."""
    actual_ml = float(telemetry.get("consumed_ml", 0.0))
    diff = abs(actual_ml - expected_ml)
    assert (
        diff <= tolerance
    ), f"Consumed ml mismatch: expected {expected_ml} ±{tolerance}, got {actual_ml} (delta={diff})"


def assert_weight_g(telemetry: Dict[str, Any], expected_g: float, tolerance: float = 0.5) -> None:
    """Assert reported weight in grams."""
    actual_g = float(telemetry.get("weight_g", 0.0))
    diff = abs(actual_g - expected_g)
    assert (
        diff <= tolerance
    ), f"Weight mismatch: expected {expected_g} ±{tolerance}, got {actual_g} (delta={diff})"


def assert_notification_sent(
    notifications: List[Dict[str, Any]],
    title: str,
    priority: Optional[int] = None,
    tag_contains: Optional[str] = None,
) -> Dict[str, Any]:
    """Assert HTTP push notification was dispatched."""
    matching = [n for n in notifications if n.get("title") == title]
    assert len(matching) > 0, f"Notification with title '{title}' not found. Sent titles: {[n.get('title') for n in notifications]}"

    target = matching[-1]
    if priority is not None:
        assert target.get("priority") == priority, f"Notification priority mismatch: expected {priority}, got {target.get('priority')}"

    if tag_contains is not None:
        actual_tags = target.get("tags", "")
        assert tag_contains in actual_tags, f"Expected tag '{tag_contains}' in tags '{actual_tags}'"

    return target


def assert_no_notification_sent(notifications: List[Dict[str, Any]], title: str) -> None:
    """Assert HTTP push notification with title was NOT sent."""
    matching = [n for n in notifications if n.get("title") == title]
    assert len(matching) == 0, f"Notification '{title}' was unexpectedly dispatched."
