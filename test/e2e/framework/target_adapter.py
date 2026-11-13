"""Device Target Adapter Abstraction for E2E Testing."""

from abc import ABC, abstractmethod
from typing import Dict, List, Any, Optional
from .virtual_device import VirtualCoasterDevice


class TargetAdapter(ABC):
    """Abstract interface for E2E test target interaction."""

    @abstractmethod
    def send_weight(self, raw_grams: float, timestamp_ms: int) -> None:
        """Inject load cell weight sample at simulated millisecond time."""
        pass

    @abstractmethod
    def send_command(self, cmd: str) -> Dict[str, Any]:
        """Send UART command string and return parsed JSON response."""
        pass

    @abstractmethod
    def get_telemetry(self) -> Dict[str, Any]:
        """Obtain latest single-line JSON telemetry packet."""
        pass

    @abstractmethod
    def get_events(self) -> List[Dict[str, Any]]:
        """Retrieve all domain events emitted so far."""
        pass

    @abstractmethod
    def get_notifications(self) -> List[Dict[str, Any]]:
        """Retrieve all HTTP push notifications dispatched so far."""
        pass

    @abstractmethod
    def get_led(self) -> Dict[str, Any]:
        """Retrieve simulated 16-LED ring state."""
        pass

    @abstractmethod
    def reset(self) -> None:
        """Reset target state."""
        pass


class VirtualDeviceAdapter(TargetAdapter):
    """Adapter executing tests against the high-fidelity VirtualCoasterDevice."""

    def __init__(self, daily_goal_ml: float = 2000.0, default_wifi: str = "CONNECTED"):
        self.daily_goal_ml = daily_goal_ml
        self.default_wifi = default_wifi
        self.device = VirtualCoasterDevice(daily_goal_ml=daily_goal_ml, default_wifi=default_wifi)

    def send_weight(self, raw_grams: float, timestamp_ms: int) -> None:
        self.device.update_weight(raw_grams, timestamp_ms)

    def send_command(self, cmd: str) -> Dict[str, Any]:
        resp_json_str = self.device.process_uart_command(cmd)
        import json
        return json.loads(resp_json_str)

    def get_telemetry(self) -> Dict[str, Any]:
        return self.device.get_telemetry_snapshot()

    def get_events(self) -> List[Dict[str, Any]]:
        return list(self.device.captured_events)

    def get_notifications(self) -> List[Dict[str, Any]]:
        return list(self.device.dispatched_notifications)

    def get_led(self) -> Dict[str, Any]:
        return self.device.get_led_state()

    def reset(self) -> None:
        self.device = VirtualCoasterDevice(daily_goal_ml=self.daily_goal_ml, default_wifi=self.default_wifi)
