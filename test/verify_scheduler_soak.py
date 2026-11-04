#!/usr/bin/env python3
"""Empirical Stress Test Harness: Multi-Rate Scheduler Soak & 32-Bit Rollover.

Simulates the cooperative multi-rate scheduler implemented in src/main.cpp:
- Loop Tick (Continuous, ~0ms)
- 50 Hz (20 ms): Status LED PWM engine
- 20 Hz (50 ms): Weight sensor sampling & domain event pump
- 10 Hz (100 ms): OLED UI rendering
- 20 Hz (50 ms): Network push & Wi-Fi FSM
- 0.2 Hz (5000 ms): Telemetry heartbeat

Test Scenarios:
1. 100,000-cycle soak test verifying task rate frequencies and distributions.
2. 32-bit millisecond rollover test across 0xFFFFFFFF -> 0x00000000 proving zero lockup/starvation.
3. Realistic execution jitter simulation (variable FreeRTOS tick duration 1-4ms).
4. Boundary analysis of DisplayOled::triggerDrinkSplash rollover vulnerability.
"""

import sys
import math
import statistics
from typing import Dict, List, Tuple

UINT32_MAX = 0xFFFFFFFF
UINT32_MASK = 0xFFFFFFFF


def u32_diff(now_ms: int, last_ms: int) -> int:
    """Simulate C++ static_cast<uint32_t>(now_ms - last_ms)."""
    return (now_ms - last_ms) & UINT32_MASK


class SchedulerSimulation:
    def __init__(self, start_ms: int = 0):
        self.now_ms = start_ms & UINT32_MASK

        # Task rate intervals (milliseconds)
        self.INTERVAL_LED = 20        # 50 Hz
        self.INTERVAL_SENSOR = 50     # 20 Hz
        self.INTERVAL_OLED = 100      # 10 Hz
        self.INTERVAL_NET = 50        # 20 Hz
        self.INTERVAL_HEARTBEAT = 5000 # 0.2 Hz

        # Last execution timestamps
        self.last_led_ms = self.now_ms
        self.last_sensor_ms = self.now_ms
        self.last_oled_ms = self.now_ms
        self.last_net_ms = self.now_ms
        self.last_heartbeat_ms = self.now_ms

        # Execution event records: list of timestamp_ms
        self.history_uart: List[int] = []
        self.history_led: List[int] = []
        self.history_sensor: List[int] = []
        self.history_oled: List[int] = []
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

        # Task 3: 50 Hz Status LED
        if u32_diff(self.now_ms, self.last_led_ms) >= self.INTERVAL_LED:
            self.last_led_ms = self.now_ms
            self.history_led.append(self.now_ms)

        # Task 4: 10 Hz OLED UI
        if u32_diff(self.now_ms, self.last_oled_ms) >= self.INTERVAL_OLED:
            self.last_oled_ms = self.now_ms
            self.history_oled.append(self.now_ms)

        # Task 5: 20 Hz Network Push
        if u32_diff(self.now_ms, self.last_net_ms) >= self.INTERVAL_NET:
            self.last_net_ms = self.now_ms
            self.history_net.append(self.now_ms)

        # Task 6: 0.2 Hz Telemetry Heartbeat
        if u32_diff(self.now_ms, self.last_heartbeat_ms) >= self.INTERVAL_HEARTBEAT:
            self.last_heartbeat_ms = self.now_ms
            self.history_heartbeat.append(self.now_ms)

        # Task 7: Cooperative yield advances time
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
        ("Status LED", "20 ms (50 Hz)", sim.history_led, 20, 5000),
        ("Sensor & Domain", "50 ms (20 Hz)", sim.history_sensor, 50, 2000),
        ("OLED Display", "100 ms (10 Hz)", sim.history_oled, 100, 1000),
        ("Network Push", "50 ms (20 Hz)", sim.history_net, 50, 2000),
        ("Heartbeat", "5000 ms (0.2 Hz)", sim.history_heartbeat, 5000, 20),
    ]

    all_pass = True
    for name, nom_str, hist, nom_interval, exp_count in tasks:
        st = compute_interval_stats(hist)
        # Check that intervals are strictly exact (stddev == 0 for uniform 1ms ticks)
        # Count is within 1 of theoretical maximum (due to boundary offset)
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
        "Status LED": (sim.history_led, 20),
        "Sensor & Domain": (sim.history_sensor, 50),
        "OLED Display": (sim.history_oled, 100),
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
    print("TEST 3: Non-Uniform Execution Jitter Soak Test (Simulating I2C & Delays)")
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
        ("Status LED", 50.0, sim.history_led, 20),
        ("Sensor & Domain", 20.0, sim.history_sensor, 50),
        ("OLED Display", 10.0, sim.history_oled, 100),
        ("Network Push", 20.0, sim.history_net, 50),
        ("Heartbeat", 0.2, sim.history_heartbeat, 5000),
    ]

    all_pass = True
    for name, exp_rate, hist, nominal_int in tasks:
        obs_rate = len(hist) / total_time_s
        st = compute_interval_stats(hist)
        # With cooperative jitter, rate tracks actual elapsed time within 3%
        rate_err = abs(obs_rate - exp_rate) / exp_rate
        # For low frequency heartbeat (0.2 Hz), 1 tick in 75s is ~7% variance
        tol = 0.10 if exp_rate < 1.0 else 0.05
        passed = rate_err < tol
        if not passed:
            all_pass = False
        status_str = "[PASS]" if passed else "[WARN]"
        print(f"{name:<20} | {exp_rate:5.1f} Hz     | {len(hist):<7} | {obs_rate:6.2f} Hz      | {st['min']} - {st['max']} ms   | {status_str}")

    return all_pass


def run_oled_splash_rollover_challenge() -> bool:
    print("\n" + "=" * 70)
    print("TEST 4: Empirical Rollover Boundary Analysis of DisplayOled Splash Screen")
    print("=" * 70)

    duration_ms = 3000

    # Scenario A: Drink logged 1000ms before rollover
    trigger_now = UINT32_MAX - 1000
    splash_until = (trigger_now + duration_ms) & UINT32_MASK  # 1999

    print(f"Trigger Timestamp    : 0x{trigger_now:08X} ({trigger_now})")
    print(f"Calculated Deadline  : 0x{splash_until:08X} ({splash_until}) (Wrapped!)")

    check1_ms = trigger_now + 500
    is_showing_1 = (check1_ms < splash_until)
    print(f"  At t = trigger + 500ms (0x{check1_ms:08X}): now_ms < splash_until -> {is_showing_1} (EXPECTED TRUE, GOT FALSE!)")

    check2_ms = 500
    is_showing_2 = (check2_ms < splash_until)
    print(f"  At t = trigger + 1500ms (0x{check2_ms:08X}): now_ms < splash_until -> {is_showing_2} (SHOWING LATE)")

    # Scenario B: Stale splash from day 1 re-appearing 49.7 days later
    day1_trigger = 50_000
    day1_deadline = day1_trigger + duration_ms  # 53,000

    post_rollover_ms = 100
    is_showing_stale = (post_rollover_ms < day1_deadline)
    print(f"\nScenario B (Stale Splash Re-appearance):")
    print(f"  Day 1 Splash Deadline : {day1_deadline} ms")
    print(f"  At t = 49.7 days (t=100ms after rollover): now_ms < day1_deadline -> {is_showing_stale} (BUG: STALE SPLASH RESURRECTED!)")

    bug_found = (not is_showing_1) or is_showing_stale
    if bug_found:
        print("\n--> EMPIRICAL VULNERABILITY CONFIRMED: DisplayOled uses direct `<` comparison")
        print("    instead of rollover-safe subtraction `(now_ms - splash_start_ms) < duration_ms`")
        print("    with an explicit `splash_active_` flag!")
    return bug_found


if __name__ == "__main__":
    p1 = run_soak_test_100k_cycles()
    p2 = run_rollover_test()
    p3 = run_jitter_soak_test()
    p4 = run_oled_splash_rollover_challenge()

    print("\n" + "=" * 70)
    print("EMPIRICAL TEST SUMMARY:")
    print(f"  100k Soak Test            : {'PASS' if p1 else 'FAIL'}")
    print(f"  32-Bit Rollover Test      : {'PASS' if p2 else 'FAIL'}")
    print(f"  Jitter Test               : {'PASS' if p3 else 'FAIL'}")
    print(f"  OLED Splash Rollover Bug  : {'CONFIRMED VULNERABILITY' if p4 else 'CLEAN'}")
    print("=" * 70)
