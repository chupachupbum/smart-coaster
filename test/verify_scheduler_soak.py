#!/usr/bin/env python3
"""Empirical Stress Test Harness: Multi-Rate Scheduler Soak & 32-Bit Rollover.

Simulates the cooperative multi-rate scheduler implemented in src/main.cpp:
- Loop Tick (Continuous, ~0ms)
- 50 Hz (20 ms): 16-Pixel WS2812B LED Ring animation engine
- 20 Hz (50 ms): Weight sensor sampling & domain event pump
- 20 Hz (50 ms): Network push & Wi-Fi FSM
- 0.2 Hz (5000 ms): Telemetry heartbeat

Test Scenarios:
1. 100,000-cycle soak test verifying task rate frequencies and distributions.
2. 32-bit millisecond rollover test across 0xFFFFFFFF -> 0x00000000 proving zero lockup/starvation.
3. Realistic execution jitter simulation (variable FreeRTOS tick duration 1-4ms).
4. LedRing transient animation rollover safety verification across 0xFFFFFFFF.
"""

import sys
import statistics
from typing import Dict, List

UINT32_MAX = 0xFFFFFFFF
UINT32_MASK = 0xFFFFFFFF


def u32_diff(now_ms: int, last_ms: int) -> int:
    """Simulate C++ static_cast<uint32_t>(now_ms - last_ms)."""
    return (now_ms - last_ms) & UINT32_MASK


class SchedulerSimulation:
    def __init__(self, start_ms: int = 0):
        self.now_ms = start_ms & UINT32_MASK

        # Task rate intervals (milliseconds)
        self.INTERVAL_LED_RING = 20    # 50 Hz
        self.INTERVAL_SENSOR = 50      # 20 Hz
        self.INTERVAL_NET = 50         # 20 Hz
        self.INTERVAL_HEARTBEAT = 5000 # 0.2 Hz

        # Last execution timestamps
        self.last_led_ms = self.now_ms
        self.last_sensor_ms = self.now_ms
        self.last_net_ms = self.now_ms
        self.last_heartbeat_ms = self.now_ms

        # Execution event records: list of timestamp_ms
        self.history_uart: List[int] = []
        self.history_led: List[int] = []
        self.history_sensor: List[int] = []
        self.history_net: List[int] = []
        self.history_heartbeat: List[int] = []

    def tick(self, step_ms: int = 1):
        """Advance time and execute scheduler loop tick (matching main.cpp)."""
        # Task 1: UART CLI continuous (every loop tick)
        self.history_uart.append(self.now_ms)

        # Task 2: 20 Hz Sensor & Domain
        if u32_diff(self.now_ms, self.last_sensor_ms) >= self.INTERVAL_SENSOR:
            self.last_sensor_ms = self.now_ms
            self.history_sensor.append(self.now_ms)

        # Task 3: 50 Hz LED Ring
        if u32_diff(self.now_ms, self.last_led_ms) >= self.INTERVAL_LED_RING:
            self.last_led_ms = self.now_ms
            self.history_led.append(self.now_ms)

        # Task 4: 20 Hz Network Push
        if u32_diff(self.now_ms, self.last_net_ms) >= self.INTERVAL_NET:
            self.last_net_ms = self.now_ms
            self.history_net.append(self.now_ms)

        # Task 5: 0.2 Hz Telemetry Heartbeat
        if u32_diff(self.now_ms, self.last_heartbeat_ms) >= self.INTERVAL_HEARTBEAT:
            self.last_heartbeat_ms = self.now_ms
            self.history_heartbeat.append(self.now_ms)

        # Task 6: Cooperative yield advances time
        self.now_ms = (self.now_ms + step_ms) & UINT32_MASK


def compute_interval_stats(history: List[int]) -> Dict[str, float]:
    if len(history) < 2:
        return {"count": len(history), "min": 0, "max": 0, "mean": 0, "stddev": 0}

    deltas = [u32_diff(history[i], history[i - 1]) for i in range(1, len(history))]
    return {
        "count": len(history),
        "min": min(deltas),
        "max": max(deltas),
        "mean": statistics.mean(deltas),
        "stddev": statistics.stdev(deltas) if len(deltas) > 1 else 0.0,
    }


def run_soak_test_100k_cycles() -> bool:
    print("\n" + "=" * 70)
    print("TEST 1: 100,000-Cycle Multi-Rate Scheduler Soak Test (1ms step)")
    print("=" * 70)

    sim = SchedulerSimulation(start_ms=0)
    TOTAL_CYCLES = 100_000  # 100,000 steps

    for _ in range(TOTAL_CYCLES):
        sim.tick(step_ms=1)

    total_time_ms = TOTAL_CYCLES * 1
    total_time_s = total_time_ms / 1000.0

    print(f"Total Cycles Simulated: {TOTAL_CYCLES:,}")
    print(f"Total Simulated Time  : {total_time_s:.1f} s ({total_time_ms:,} ms)")
    print("-" * 70)
    print(f"{'Task':<20} | {'Nominal Interval':<16} | {'Count':<7} | {'Mean Delta':<10} | {'StdDev':<8} | {'Status'}")
    print("-" * 70)

    tasks = [
        ("UART CLI (Cont.)", "Continuous (1ms)", sim.history_uart, 1, 100_000),
        ("LED Ring (50 Hz)", "20 ms (50 Hz)", sim.history_led, 20, 5000),
        ("Sensor & Domain", "50 ms (20 Hz)", sim.history_sensor, 50, 2000),
        ("Network Push", "50 ms (20 Hz)", sim.history_net, 50, 2000),
        ("Heartbeat", "5000 ms (0.2 Hz)", sim.history_heartbeat, 5000, 20),
    ]

    all_pass = True
    for name, nom_str, hist, nom_interval, exp_count in tasks:
        st = compute_interval_stats(hist)
        passed = (st["min"] == nom_interval or st["min"] == 0) and (st["max"] == nom_interval or st["max"] == 0) and (abs(len(hist) - exp_count) <= 1)
        if not passed:
            all_pass = False
        status_str = "[PASS]" if passed else "[FAIL]"
        print(f"{name:<20} | {nom_str:<16} | {len(hist):<7} | {st['mean']:6.2f} ms | {st['stddev']:6.4f} | {status_str}")

    return all_pass


def run_rollover_test() -> bool:
    print("\n" + "=" * 70)
    print("TEST 2: 32-Bit Millisecond Rollover Test (0xFFFFFFFF -> 0x00000000)")
    print("=" * 70)

    # Start 50,000ms (50 seconds) before 32-bit overflow
    start_ms = UINT32_MAX - 50_000
    sim = SchedulerSimulation(start_ms=start_ms)

    TOTAL_CYCLES = 100_000  # 50,000ms before rollover, 50,000ms after rollover

    rollover_cycle = None
    for cycle in range(TOTAL_CYCLES):
        prev_ms = sim.now_ms
        sim.tick(step_ms=1)
        if prev_ms > sim.now_ms and rollover_cycle is None:
            rollover_cycle = cycle

    print(f"Start Timestamp     : 0x{start_ms:08X} ({start_ms})")
    print(f"Rollover Timestamp  : 0xFFFFFFFF -> 0x00000000 at cycle {rollover_cycle}")
    print(f"End Timestamp       : 0x{sim.now_ms:08X} ({sim.now_ms})")
    print(f"Total Cycles Passed : {TOTAL_CYCLES:,}")
    print("-" * 70)

    all_pass = True
    histories = {
        "LED Ring (50 Hz)": (sim.history_led, 20),
        "Sensor & Domain": (sim.history_sensor, 50),
        "Network Push": (sim.history_net, 50),
        "Heartbeat": (sim.history_heartbeat, 5000),
    }

    print(f"{'Task':<20} | {'Executions':<10} | {'Rollover Delta':<16} | {'Expected Delta':<16} | {'Status'}")
    print("-" * 70)

    for task_name, (hist, expected_interval) in histories.items():
        act_count = len(hist)

        # Find the transition that crossed 0xFFFFFFFF
        rollover_delta = None
        for i in range(1, len(hist)):
            t_prev = hist[i - 1]
            t_curr = hist[i]
            if t_curr < t_prev:  # Rollover happened between these two triggers
                rollover_delta = u32_diff(t_curr, t_prev)
                break

        passed = (rollover_delta == expected_interval) and (act_count > 0)
        if not passed:
            all_pass = False

        status_str = "[PASS]" if passed else "[FAIL]"
        print(f"{task_name:<20} | {act_count:<10} | {str(rollover_delta) + ' ms':<16} | {str(expected_interval) + ' ms':<16} | {status_str}")

    return all_pass


def run_jitter_soak_test() -> bool:
    print("\n" + "=" * 70)
    print("TEST 3: Non-Uniform Execution Jitter Soak Test (Simulating Delays)")
    print("=" * 70)

    sim = SchedulerSimulation(start_ms=0)
    TOTAL_CYCLES = 50_000

    seed = 12345
    def pseudo_rand_step():
        nonlocal seed
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        val = seed % 10
        if val < 7:
            return 1
        elif val < 9:
            return 2
        else:
            return 4

    step_sum_ms = 0
    for _ in range(TOTAL_CYCLES):
        step = pseudo_rand_step()
        step_sum_ms += step
        sim.tick(step_ms=step)

    total_time_s = step_sum_ms / 1000.0
    print(f"Simulated Cycles     : {TOTAL_CYCLES:,}")
    print(f"Simulated Wall Time  : {total_time_s:.2f} s ({step_sum_ms:,} ms)")
    print("-" * 70)
    print(f"{'Task':<20} | {'Target Rate':<12} | {'Count':<7} | {'Observed Rate':<14} | {'Min/Max Delta':<14} | {'Status'}")
    print("-" * 70)

    tasks = [
        ("LED Ring", 50.0, sim.history_led, 20),
        ("Sensor & Domain", 20.0, sim.history_sensor, 50),
        ("Network Push", 20.0, sim.history_net, 50),
        ("Heartbeat", 0.2, sim.history_heartbeat, 5000),
    ]

    all_pass = True
    for name, exp_rate, hist, nominal_int in tasks:
        obs_rate = len(hist) / total_time_s
        st = compute_interval_stats(hist)
        rate_err = abs(obs_rate - exp_rate) / exp_rate
        tol = 0.10 if exp_rate < 1.0 else 0.05
        passed = rate_err < tol
        if not passed:
            all_pass = False
        status_str = "[PASS]" if passed else "[WARN]"
        print(f"{name:<20} | {exp_rate:5.1f} Hz     | {len(hist):<7} | {obs_rate:6.2f} Hz      | {st['min']} - {st['max']} ms   | {status_str}")

    return all_pass


def run_led_ring_rollover_safety_test() -> bool:
    print("\n" + "=" * 70)
    print("TEST 4: LedRing Transient Animation 32-Bit Rollover Safety Check")
    print("=" * 70)

    def anim_is_active(active_flag: bool, start_ms: int, now_ms: int, duration_ms: int) -> bool:
        if not active_flag:
            return False
        return u32_diff(now_ms, start_ms) < duration_ms

    # Scenario A: 3000ms RAINBOW_GOAL / INTAKE_ARC triggered 1000ms before rollover
    duration_ms = 3000
    trigger_ms = UINT32_MAX - 1000
    active = True

    at_500 = (trigger_ms + 500) & UINT32_MASK
    at_1500 = (trigger_ms + 1500) & UINT32_MASK  # Wrapped past 0
    at_2999 = (trigger_ms + 2999) & UINT32_MASK
    at_3000 = (trigger_ms + 3000) & UINT32_MASK

    ok_500 = anim_is_active(active, trigger_ms, at_500, duration_ms) is True
    ok_1500 = anim_is_active(active, trigger_ms, at_1500, duration_ms) is True
    ok_2999 = anim_is_active(active, trigger_ms, at_2999, duration_ms) is True
    ok_3000 = anim_is_active(active, trigger_ms, at_3000, duration_ms) is False

    # Scenario B: Expired animation with active=False never resurrects after rollover
    day1_trigger = 50_000
    active_after_expiry = False
    post_rollover_ms = 51_000
    no_resurrection = anim_is_active(active_after_expiry, day1_trigger, post_rollover_ms, duration_ms) is False

    print(f"  Trigger at 0x{trigger_ms:08X}, duration {duration_ms}ms:")
    print(f"    +500ms  (0x{at_500:08X}): active={ok_500}")
    print(f"    +1500ms (0x{at_1500:08X}): active={ok_1500}")
    print(f"    +2999ms (0x{at_2999:08X}): active={ok_2999}")
    print(f"    +3000ms (0x{at_3000:08X}): expired={ok_3000}")
    print(f"    Expired flag post-rollover check: safe={no_resurrection}")

    passed = ok_500 and ok_1500 and ok_2999 and ok_3000 and no_resurrection
    return passed


if __name__ == "__main__":
    p1 = run_soak_test_100k_cycles()
    p2 = run_rollover_test()
    p3 = run_jitter_soak_test()
    p4 = run_led_ring_rollover_safety_test()

    print("\n" + "=" * 70)
    print("EMPIRICAL TEST SUMMARY:")
    print(f"  100k Soak Test            : {'PASS' if p1 else 'FAIL'}")
    print(f"  32-Bit Rollover Test      : {'PASS' if p2 else 'FAIL'}")
    print(f"  Jitter Test               : {'PASS' if p3 else 'FAIL'}")
    print(f"  LedRing Rollover Safety   : {'PASS' if p4 else 'FAIL'}")
    print("=" * 70)
    sys.exit(0 if (p1 and p2 and p3 and p4) else 1)
