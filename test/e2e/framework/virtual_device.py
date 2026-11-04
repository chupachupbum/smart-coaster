"""High-Fidelity Virtual Device Reference Model for Smart Cup Stand.

This module provides an opaque-box behavioral simulator implementing the complete
system specification from PROJECT.md and ORIGINAL_REQUEST.md.
"""

from collections import deque
import json
import math
from typing import Dict, List, Optional, Tuple, Any


class VirtualCoasterDevice:
    """Virtual Coaster Device implementing authoritative domain & interface requirements."""

    # Domain constants
    AUTO_TARE_WINDOW_G = 5.0
    AUTO_TARE_MIN_DRIFT_G = 0.05
    AUTO_TARE_TIME_MS_NORMAL = 5000
    AUTO_TARE_TIME_MS_DEMO = 1000

    CUP_PRESENCE_MIN_G = 30.0
    CUP_LIFT_MAX_G = 20.0

    STABILITY_MAX_SPAN_G = 3.0
    STABILITY_WINDOW_MS = 1500
    STABILITY_MIN_SAMPLES = 3

    DRINK_MIN_DELTA_G = 15.0
    REFILL_MIN_DELTA_G = 30.0

    REMINDER_INTERVAL_MS_NORMAL = 45 * 60 * 1000  # 45 minutes
    REMINDER_INTERVAL_MS_DEMO = 45 * 1000         # 45 seconds

    REPEAT_INTERVAL_MS_NORMAL = 5 * 60 * 1000    # 5 minutes
    REPEAT_INTERVAL_MS_DEMO = 5 * 1000           # 5 seconds

    MAX_QUEUE_CAPACITY = 3

    def __init__(self, daily_goal_ml: float = 2000.0, default_wifi: str = "CONNECTED"):
        self.daily_goal_ml = float(daily_goal_ml)
        self.wifi_status = default_wifi

        # Device operational state
        self.cup_state = "EMPTY"  # EMPTY, STABILIZING, CUP_PRESENT, CUP_LIFTED, RETURN_STABILIZING
        self.alert_state = "IDLE"  # IDLE, COUNTING, ALERT_DUE, ALERT_REPEAT
        self.demo_mode = False

        # Weight registers
        self.current_raw_weight_g = 0.0
        self.tare_offset_g = 0.0
        self.baseline_weight_g = 0.0
        self.consumed_today_ml = 0.0
        self.drink_count = 0

        # Stability sliding buffer: list of (timestamp_ms, net_weight_g)
        self.stability_samples: List[Tuple[int, float]] = []
        self.is_stable = False
        self.stable_weight_g = 0.0

        # Auto-tare tracking
        self.auto_tare_start_ms: Optional[int] = None
        self.auto_tare_last_ms: Optional[int] = None

        # Reminder tracking
        self.last_drink_time_ms = 0
        self.last_alert_time_ms = 0
        self.session_active = False
        self.goal_notified = False

        # Clocks and buffers
        self.current_time_ms = 0
        self.uart_output_buffer: List[str] = []
        self.notification_queue: deque = deque(maxlen=self.MAX_QUEUE_CAPACITY)
        self.dispatched_notifications: List[Dict[str, Any]] = []
        self.captured_events: List[Dict[str, Any]] = []

    @property
    def net_weight_g(self) -> float:
        """Current weight adjusted by tare offset."""
        return self.current_raw_weight_g - self.tare_offset_g

    @property
    def auto_tare_threshold_ms(self) -> int:
        return self.AUTO_TARE_TIME_MS_DEMO if self.demo_mode else self.AUTO_TARE_TIME_MS_NORMAL

    @property
    def reminder_interval_ms(self) -> int:
        return self.REMINDER_INTERVAL_MS_DEMO if self.demo_mode else self.REMINDER_INTERVAL_MS_NORMAL

    @property
    def repeat_interval_ms(self) -> int:
        return self.REPEAT_INTERVAL_MS_DEMO if self.demo_mode else self.REPEAT_INTERVAL_MS_NORMAL

    def update_weight(self, raw_weight_g: float, timestamp_ms: int) -> None:
        """Process incoming raw load cell sample."""
        self.current_time_ms = timestamp_ms
        self.current_raw_weight_g = float(raw_weight_g)
        net_w = self.net_weight_g

        # 1. Update stability filter buffer
        self._record_stability_sample(timestamp_ms, net_w)
        self._evaluate_stability(timestamp_ms)

        # 2. State Machine Transitions
        self._tick_state_machine(timestamp_ms, net_w)

        # 3. Reminder & Alert Timing
        self._tick_reminder_timers(timestamp_ms)

    def _record_stability_sample(self, timestamp_ms: int, weight: float) -> None:
        self.stability_samples.append((timestamp_ms, weight))
        # Purge samples older than 1500 ms
        cutoff = timestamp_ms - self.STABILITY_WINDOW_MS
        self.stability_samples = [(t, w) for (t, w) in self.stability_samples if t >= cutoff]

    def _evaluate_stability(self, timestamp_ms: int) -> None:
        if not self.stability_samples:
            self.is_stable = False
            self.stable_weight_g = self.net_weight_g
            return

        oldest_t = self.stability_samples[0][0]
        weights = [w for (_, w) in self.stability_samples]
        min_w = min(weights)
        max_w = max(weights)
        span = max_w - min_w
        duration = timestamp_ms - oldest_t

        if (
            len(self.stability_samples) >= self.STABILITY_MIN_SAMPLES
            and duration >= self.STABILITY_WINDOW_MS
            and span <= self.STABILITY_MAX_SPAN_G
        ):
            self.is_stable = True
            self.stable_weight_g = sum(weights) / len(weights)
        else:
            self.is_stable = False
            self.stable_weight_g = self.net_weight_g

    def _tick_state_machine(self, timestamp_ms: int, net_w: float) -> None:
        if self.cup_state == "EMPTY":
            # Auto-tare logic: empty state and within [0.05g, 5.0g]
            if self.AUTO_TARE_MIN_DRIFT_G <= abs(net_w) <= self.AUTO_TARE_WINDOW_G:
                if self.auto_tare_start_ms is None:
                    self.auto_tare_start_ms = timestamp_ms
                elif timestamp_ms - self.auto_tare_start_ms >= self.auto_tare_threshold_ms:
                    # Trigger auto-tare
                    self.tare_offset_g += net_w
                    self.auto_tare_start_ms = None
                    self._emit_event(
                        "AUTO_TARE",
                        {"tare_offset": round(self.tare_offset_g, 2)},
                    )
            else:
                self.auto_tare_start_ms = None

            # Cup presence transition
            if net_w > self.CUP_PRESENCE_MIN_G:
                self.cup_state = "STABILIZING"
                self.stability_samples.clear()
                self.auto_tare_start_ms = None
                self._emit_event(
                    "STATE_CHANGE",
                    {"old_state": "EMPTY", "new_state": "STABILIZING"},
                )

        elif self.cup_state == "STABILIZING":
            # Dropped below threshold before stabilizing
            if net_w <= self.CUP_PRESENCE_MIN_G:
                self.cup_state = "EMPTY"
                self.stability_samples.clear()
                self._emit_event(
                    "STATE_CHANGE",
                    {"old_state": "STABILIZING", "new_state": "EMPTY"},
                )
            elif self.is_stable and self.stable_weight_g > self.CUP_PRESENCE_MIN_G:
                self.cup_state = "CUP_PRESENT"
                self.baseline_weight_g = self.stable_weight_g
                self.alert_state = "COUNTING"
                self.last_drink_time_ms = timestamp_ms
                self.session_active = True
                self._emit_event(
                    "STATE_CHANGE",
                    {
                        "old_state": "STABILIZING",
                        "new_state": "CUP_PRESENT",
                        "baseline_g": round(self.baseline_weight_g, 1),
                    },
                )

        elif self.cup_state == "CUP_PRESENT":
            # Lift detected when weight drops below 20g
            if net_w < self.CUP_LIFT_MAX_G:
                self.cup_state = "CUP_LIFTED"
                self.stability_samples.clear()
                self._emit_event(
                    "STATE_CHANGE",
                    {"old_state": "CUP_PRESENT", "new_state": "CUP_LIFTED"},
                )

        elif self.cup_state == "CUP_LIFTED":
            # Return detected when weight exceeds 30g
            if net_w > self.CUP_PRESENCE_MIN_G:
                self.cup_state = "RETURN_STABILIZING"
                self.stability_samples.clear()
                self._emit_event(
                    "STATE_CHANGE",
                    {"old_state": "CUP_LIFTED", "new_state": "RETURN_STABILIZING"},
                )

        elif self.cup_state == "RETURN_STABILIZING":
            # Cup lifted again before stabilizing
            if net_w < self.CUP_LIFT_MAX_G:
                self.cup_state = "CUP_LIFTED"
                self.stability_samples.clear()
                self._emit_event(
                    "STATE_CHANGE",
                    {"old_state": "RETURN_STABILIZING", "new_state": "CUP_LIFTED"},
                )
            elif self.is_stable and self.stable_weight_g > self.CUP_PRESENCE_MIN_G:
                return_w = self.stable_weight_g
                delta = self.baseline_weight_g - return_w
                self.cup_state = "CUP_PRESENT"

                if delta >= self.DRINK_MIN_DELTA_G:
                    # Valid drink logged
                    self.consumed_today_ml += delta
                    self.drink_count += 1
                    self.baseline_weight_g = return_w
                    self.alert_state = "COUNTING"
                    self.last_drink_time_ms = timestamp_ms
                    self._emit_event(
                        "DRINK_LOGGED",
                        {
                            "drink_delta_ml": round(delta, 1),
                            "total_consumed_ml": round(self.consumed_today_ml, 1),
                            "baseline_g": round(self.baseline_weight_g, 1),
                        },
                    )
                    self._dispatch_push_notification(
                        title="Drink Logged - Smart Coaster",
                        message=f"Logged {round(delta)}ml! Total today: {round(self.consumed_today_ml)}/{round(self.daily_goal_ml)}ml.",
                        priority=3,
                        tags="glass_of_milk,droplet",
                    )
                    # Check goal completion
                    if self.consumed_today_ml >= self.daily_goal_ml and not self.goal_notified:
                        self.goal_notified = True
                        self._emit_event(
                            "GOAL_REACHED",
                            {"goal_ml": self.daily_goal_ml, "consumed_ml": round(self.consumed_today_ml, 1)},
                        )
                        self._dispatch_push_notification(
                            title="Daily Goal Achieved!",
                            message=f"Congratulations! You reached your {round(self.daily_goal_ml)}ml hydration goal!",
                            priority=3,
                            tags="tada,trophy",
                        )

                elif (return_w - self.baseline_weight_g) >= (self.REFILL_MIN_DELTA_G - 0.001):
                    # Refill detected
                    refill_gain = return_w - self.baseline_weight_g
                    self.baseline_weight_g = return_w
                    self._emit_event(
                        "REFILL_DETECTED",
                        {
                            "refill_delta_ml": round(refill_gain, 1),
                            "baseline_g": round(self.baseline_weight_g, 1),
                        },
                    )
                else:
                    # Lift without drinking (< 15g reduction and < 30g gain)
                    self.baseline_weight_g = return_w  # Update baseline to prevent drift creep
                    self._emit_event(
                        "LIFT_NO_DRINK",
                        {"baseline_g": round(self.baseline_weight_g, 1)},
                    )

    def _tick_reminder_timers(self, timestamp_ms: int) -> None:
        if not self.session_active or self.cup_state == "EMPTY":
            return

        if self.alert_state == "COUNTING":
            if timestamp_ms - self.last_drink_time_ms >= self.reminder_interval_ms:
                self.alert_state = "ALERT_DUE"
                self.last_alert_time_ms = timestamp_ms
                self._emit_event("REMINDER_ALERT", {"alert_count": 0})
                self._dispatch_push_notification(
                    title="Hydration Alert",
                    message="Time to drink! You haven't taken a drink in 45 minutes.",
                    priority=4,
                    tags="alarm_clock,warning",
                )

        elif self.alert_state in ("ALERT_DUE", "ALERT_REPEAT"):
            if timestamp_ms - self.last_alert_time_ms >= self.repeat_interval_ms:
                self.alert_state = "ALERT_REPEAT"
                self.last_alert_time_ms = timestamp_ms
                self._emit_event("REPEAT_ALERT", {})
                self._dispatch_push_notification(
                    title="Hydration Alert (Repeat)",
                    message="Hydration reminder: Still waiting for you to drink water!",
                    priority=4,
                    tags="repeat,warning",
                )

    def _emit_event(self, event_name: str, event_data: Dict[str, Any]) -> None:
        evt = {
            "type": "event",
            "ts": self.current_time_ms,
            "state": self.cup_state,
            "weight_g": round(self.net_weight_g, 1),
            "stable_g": round(self.stable_weight_g, 1),
            "stable": self.is_stable,
            "consumed_ml": round(self.consumed_today_ml, 1),
            "goal_ml": round(self.daily_goal_ml, 1),
            "progress_pct": round(self.get_progress_pct(), 1),
            "countdown_s": self.get_countdown_seconds(),
            "alert": self.is_alert_active(),
            "demo": self.demo_mode,
            "wifi": self.wifi_status,
            "event": event_name,
            "event_data": event_data,
        }
        self.captured_events.append(evt)
        self.uart_output_buffer.append(json.dumps(evt))

    def _dispatch_push_notification(self, title: str, message: str, priority: int, tags: str) -> None:
        notif = {
            "title": title,
            "message": message,
            "priority": priority,
            "tags": tags,
            "timestamp_ms": self.current_time_ms,
        }
        self.notification_queue.append(notif)
        self.dispatched_notifications.append(notif)

    def process_uart_command(self, cmd_line: str) -> str:
        """Process an interactive UART command and return structured JSON response."""
        clean_cmd = cmd_line.strip()
        cmd_upper = clean_cmd.upper()

        if cmd_upper == "DEMO ON":
            self.demo_mode = True
            resp = {"status": "OK", "command": "DEMO ON", "demo": True, "scale": "60x"}
        elif cmd_upper == "DEMO OFF":
            self.demo_mode = False
            resp = {"status": "OK", "command": "DEMO OFF", "demo": False, "scale": "1x"}
        elif cmd_upper == "TARE":
            self.tare_offset_g += self.net_weight_g
            resp = {
                "status": "OK",
                "command": "TARE",
                "raw_offset": round(self.tare_offset_g, 1),
                "weight_g": 0.0,
            }
        elif cmd_upper == "RESET":
            self.consumed_today_ml = 0.0
            self.drink_count = 0
            self.alert_state = "COUNTING" if self.cup_state != "EMPTY" else "IDLE"
            self.last_drink_time_ms = self.current_time_ms
            self.last_alert_time_ms = 0
            self.goal_notified = False
            resp = {
                "status": "OK",
                "command": "RESET",
                "consumed_ml": 0,
                "goal_ml": round(self.daily_goal_ml),
            }
        elif cmd_upper.startswith("GOAL"):
            parts = clean_cmd.split()
            if len(parts) >= 2 and parts[1].isdigit():
                val = int(parts[1])
                if 100 <= val <= 10000:
                    self.daily_goal_ml = float(val)
                    resp = {
                        "status": "OK",
                        "command": "GOAL",
                        "goal_ml": val,
                        "progress_pct": round(self.get_progress_pct(), 1),
                    }
                else:
                    resp = {
                        "status": "ERROR",
                        "command": "GOAL",
                        "msg": "Goal must be between 100 and 10000 ml",
                    }
            else:
                resp = {
                    "status": "ERROR",
                    "command": "GOAL",
                    "msg": "Invalid goal syntax. Expected: GOAL <ml>",
                }
        elif cmd_upper == "STATUS":
            telemetry = self.get_telemetry_snapshot()
            telemetry["type"] = "response"
            resp = telemetry
        else:
            resp = {
                "status": "ERROR",
                "command": clean_cmd,
                "msg": "Supported: DEMO ON|OFF, TARE, RESET, GOAL <ml>, STATUS",
            }

        resp_str = json.dumps(resp)
        self.uart_output_buffer.append(resp_str)
        return resp_str

    def get_countdown_seconds(self) -> int:
        if self.alert_state in ("ALERT_DUE", "ALERT_REPEAT"):
            return 0
        if not self.session_active or self.cup_state == "EMPTY":
            return self.reminder_interval_ms // 1000

        elapsed = self.current_time_ms - self.last_drink_time_ms
        remaining = max(0, self.reminder_interval_ms - elapsed)
        return remaining // 1000

    def get_progress_pct(self) -> float:
        if self.daily_goal_ml <= 0:
            return 0.0
        return (self.consumed_today_ml / self.daily_goal_ml) * 100.0

    def is_alert_active(self) -> bool:
        return self.alert_state in ("ALERT_DUE", "ALERT_REPEAT")

    def get_telemetry_snapshot(self) -> Dict[str, Any]:
        return {
            "type": "telemetry",
            "ts": self.current_time_ms,
            "state": self.cup_state,
            "weight_g": round(self.net_weight_g, 1),
            "stable_g": round(self.stable_weight_g, 1),
            "stable": self.is_stable,
            "consumed_ml": round(self.consumed_today_ml, 1),
            "goal_ml": round(self.daily_goal_ml, 1),
            "progress_pct": round(self.get_progress_pct(), 1),
            "countdown_s": self.get_countdown_seconds(),
            "alert": self.is_alert_active(),
            "demo": self.demo_mode,
            "wifi": self.wifi_status,
            "event": "NONE",
            "event_data": {},
        }

    def get_display_state(self) -> Dict[str, Any]:
        """Returns the simulated OLED display zones."""
        badge = {
            "EMPTY": "[EMPTY]",
            "STABILIZING": "[WAIT]",
            "CUP_PRESENT": "[CUP]",
            "CUP_LIFTED": "[LIFT]",
            "RETURN_STABILIZING": "[WAIT]",
        }.get(self.cup_state, "[EMPTY]")

        if self.is_alert_active():
            badge = "[ALERT]"

        return {
            "badge": badge,
            "wifi": f"WIFI:{self.wifi_status}",
            "demo": "[DEMO]" if self.demo_mode else "",
            "weight_display": f"{round(self.net_weight_g, 1)} g",
            "stable_indicator": "[STBL]" if self.is_stable else "[UNST]",
            "countdown_text": f"Next reminder: {self.get_countdown_seconds()}s",
            "progress_text": f"Intake: {round(self.consumed_today_ml)}/{round(self.daily_goal_ml)} ml ({round(self.get_progress_pct())}%)",
        }

    def get_led_state(self) -> Dict[str, Any]:
        """Returns the simulated LED state and duty cycle."""
        if self.is_alert_active():
            # Sinusoidal breathing pulse
            period = 500 if self.demo_mode else 1500
            phase = self.current_time_ms % period
            angle = phase * (2.0 * math.pi / period)
            norm = (math.sin(angle - math.pi / 2.0) + 1.0) * 0.5
            duty = int(norm * norm * 255.0)
            return {"mode": "BREATHING", "pattern": "BREATHING", "duty": duty, "is_alert": True}
        elif self.cup_state in ("STABILIZING", "RETURN_STABILIZING"):
            duty = 50 if (self.current_time_ms % 1000 < 100) else 0
            return {"mode": "BLINK", "pattern": "BLINKING", "duty": duty, "is_alert": False}
        elif self.cup_state == "CUP_PRESENT":
            return {"mode": "STEADY_LOW", "pattern": "SOLID_LOW", "duty": 8, "is_alert": False}
        elif self.cup_state == "CUP_LIFTED":
            return {"mode": "SOLID_MED", "pattern": "SOLID_MED", "duty": 38, "is_alert": False}
        else:
            return {"mode": "OFF", "pattern": "OFF", "duty": 0, "is_alert": False}
