"""E2E Test Harness providing realistic device stimulation and observation."""

from typing import Dict, List, Any, Optional
from .target_adapter import TargetAdapter, VirtualDeviceAdapter


class E2ETestHarness:
    """Test harness for driving and inspecting smart cup coaster E2E interactions."""

    def __init__(self, adapter: Optional[TargetAdapter] = None, daily_goal_ml: float = 2000.0):
        self.adapter = adapter if adapter is not None else VirtualDeviceAdapter(daily_goal_ml=daily_goal_ml)
        self.current_time_ms = 1000
        self.current_weight_g = 0.0

    @property
    def telemetry(self) -> Dict[str, Any]:
        return self.adapter.get_telemetry()

    @property
    def events(self) -> List[Dict[str, Any]]:
        return self.adapter.get_events()

    @property
    def notifications(self) -> List[Dict[str, Any]]:
        return self.adapter.get_notifications()

    @property
    def display(self) -> Dict[str, Any]:
        return self.adapter.get_display()

    @property
    def led(self) -> Dict[str, Any]:
        return self.adapter.get_led()

    def tick(self, duration_ms: int = 100, step_ms: int = 50, weight_g: Optional[float] = None) -> None:
        """Advance time by duration_ms feeding constant weight_g (or previous weight)."""
        if weight_g is not None:
            self.current_weight_g = float(weight_g)

        elapsed = 0
        while elapsed < duration_ms:
            step = min(step_ms, duration_ms - elapsed)
            self.current_time_ms += step
            elapsed += step
            self.adapter.send_weight(self.current_weight_g, self.current_time_ms)

    def advance_time(self, duration_ms: int, step_ms: int = 1000) -> None:
        """Fast-forward device time keeping weight constant."""
        self.tick(duration_ms=duration_ms, step_ms=step_ms, weight_g=self.current_weight_g)

    def place_cup(self, weight_g: float, duration_ms: int = 2000, step_ms: int = 50) -> None:
        """Place cup on coaster and wait for stability window to settle."""
        self.tick(duration_ms=duration_ms, step_ms=step_ms, weight_g=weight_g)

    def lift_cup(self, duration_ms: int = 1500, step_ms: int = 50) -> None:
        """Lift cup off coaster (weight drops to 0.0g)."""
        self.tick(duration_ms=duration_ms, step_ms=step_ms, weight_g=0.0)

    def return_cup(self, weight_g: float, duration_ms: int = 2000, step_ms: int = 50) -> None:
        """Return cup onto coaster at weight_g and wait for stability window."""
        self.tick(duration_ms=duration_ms, step_ms=step_ms, weight_g=weight_g)

    def take_drink(self, sip_ml: float, duration_ms: int = 2000, step_ms: int = 50) -> None:
        """Simulate lifting cup, taking a sip of sip_ml (1ml = 1g), and replacing cup."""
        prev_weight = self.current_weight_g
        self.lift_cup(duration_ms=duration_ms, step_ms=step_ms)
        new_weight = max(0.0, prev_weight - sip_ml)
        self.return_cup(new_weight, duration_ms=duration_ms, step_ms=step_ms)

    def refill_cup(self, refill_ml: float, duration_ms: int = 2000, step_ms: int = 50) -> None:
        """Simulate lifting cup, adding beverage, and returning cup heavier."""
        prev_weight = self.current_weight_g
        self.lift_cup(duration_ms=duration_ms, step_ms=step_ms)
        new_weight = prev_weight + refill_ml
        self.return_cup(new_weight, duration_ms=duration_ms, step_ms=step_ms)

    def send_command(self, cmd: str) -> Dict[str, Any]:
        """Send UART command string and return JSON response."""
        return self.adapter.send_command(cmd)

    def clear_history(self) -> None:
        """Reset the underlying adapter and start fresh."""
        self.adapter.reset()
        self.current_time_ms = 1000
        self.current_weight_g = 0.0
