# ESP32-C3 Smart Coaster

Firmware and Wokwi hardware simulation for an ESP32-C3 hydration tracking coaster. Unlike Time-of-Flight (ToF) distance coasters that only detect whether a bottle was picked up, this system uses a 4-cell Wheatstone bridge load cell and HX711 24-bit ADC to measure actual beverage mass deltas—distinguishing real drinks from fidget lifts and refills while driving an SSD1306 OLED UI, PWM breathing LED alerts, UART JSON telemetry, and `ntfy.sh` HTTP push notifications.

![Hardware Schematic](./schematic.jpeg)

## Architecture

The firmware separates hardware-independent hydration state machines from ESP32-C3 peripherals and network services:

- **Core Domain (`lib/coaster_domain/`)**: Pure C++17 state machine (`CupState`, `AlertState`) with zero `<Arduino.h>` dependencies, enabling native host unit testing.
- **Hardware Abstraction Layer (`src/hal/`)**:
  - `WeightSensor`: Non-blocking HX711 driver (`0.42` counts/g default calibration for Wokwi 5 kg load cell).
  - `DisplayOled`: SSD1306 128×64 I2C display renderer (`0x3C`, 400 kHz Fast I2C) showing state badge, Wi-Fi status, live weight, countdown timer, and daily goal progress bar.
  - `StatusLed`: Non-blocking 5 kHz, 8-bit LEDC PWM sinusoidal breathing and celebration flash engine.
- **Telemetry & CLI (`src/telemetry/`)**: Single-line JSON event/heartbeat emitter and non-blocking UART0 command parser using fixed static buffers (zero heap allocation in steady state).
- **Network Push (`src/net/`)**: Non-blocking Wi-Fi station manager with exponential backoff and bounded FIFO queue (`maxlen=3`, 2000 ms rate limit) dispatching HTTP POST notifications to `ntfy.sh`.
- **Cooperative Scheduler (`src/main.cpp`)**: Rollover-safe multi-rate loop running UART RX polling (continuous), LED PWM (50 Hz), weight sampling + domain ticks (20 Hz), network dispatch (20 Hz), OLED rendering (10 Hz), and telemetry heartbeats (0.2 Hz).

```
├── platformio.ini              # Build & test config (esp32-c3-devkitm-1 & native)
├── diagram.json                # Wokwi circuit schematic
├── wokwi.toml                  # Wokwi firmware binary linkage
├── schematic.jpeg              # Hardware wiring diagram
├── include/
│   ├── pins_config.h           # ESP32-C3 pinout & strapping-pin guards
│   ├── config.h                # Wi-Fi, ntfy.sh, and domain defaults
│   └── secrets.example.h       # Template for local include/secrets.h (gitignored)
├── lib/
│   └── coaster_domain/         # Pure C++ hydration domain state machine
├── src/
│   ├── main.cpp                # Multi-rate cooperative scheduler
│   ├── hal/                    # HX711, SSD1306 OLED, and LEDC PWM drivers
│   ├── telemetry/              # UART CLI & single-line JSON telemetry
│   └── net/                    # Wi-Fi manager & ntfy.sh HTTP client
└── test/
    ├── test_coaster_domain/    # Native Unity unit tests (domain logic)
    ├── test_pins_config/       # Native Unity unit tests (pin safety invariants)
    └── e2e/                    # 5-tier opaque-box E2E & adversarial test suite
```

## Hydration State Machine & Thresholds

| Parameter | Normal Mode | Demo Mode (`DEMO ON`) | Behavior |
| :--- | :--- | :--- | :--- |
| **Stability Filter** | 1.5 s window, span ≤ 3.0 g | 1.5 s window, span ≤ 3.0 g | Sliding window (`≥ 3` samples); reports arithmetic mean once settled |
| **Empty Auto-Tare** | `±5.0 g` for 5.0 s | `±5.0 g` for 1.0 s | Automatically zeroes load cell drift (`≥ 0.05 g`) while in `EMPTY` |
| **Cup Presence** | `> 30.0 g` (stable) | `> 30.0 g` (stable) | Transitions `EMPTY` → `STABILIZING` → `CUP_PRESENT` and locks baseline |
| **Cup Lift** | `< 20.0 g` | `< 20.0 g` | Transitions `CUP_PRESENT` → `CUP_LIFTED` (hysteresis prevents chatter) |
| **Valid Drink** | `Δ ≤ -15.0 g` | `Δ ≤ -15.0 g` | Logs `baseline - returned` (g ≈ ml) to daily intake, clears alerts, resets timer |
| **Lift Without Drink** | `-15.0 g < Δ < +30.0 g` | `-15.0 g < Δ < +30.0 g` | Updates baseline to track condensation/drift without logging intake or resetting timer |
| **Refill Detection** | `Δ ≥ +30.0 g` | `Δ ≥ +30.0 g` | Updates baseline to new heavier weight; does not log intake or reset timer |
| **Primary Reminder** | 45 minutes | 45 seconds | Enters `ALERT_DUE`, pulses status LED, shows `[ALERT]`, dispatches push notification |
| **Repeat Reminder** | Every 5 minutes | Every 5 seconds | Enters `ALERT_REPEAT`, re-notifies every interval until a valid drink is logged |
| **Daily Goal** | 2000 ml (default) | 2000 ml (default) | Configurable `100..10000 ml`; triggers celebration LED & push notification at 100% |

## Hardware Pinout (ESP32-C3 DevKitM-1)

ESP32-C3 strapping pins (**GPIO 2**, **GPIO 8**, and **GPIO 9**) are intentionally left unconnected so external loads cannot hold `GPIO 9` low during reset or conflict with the onboard WS2812 RGB LED on `GPIO 8`.

| Peripheral | Signal | ESP32-C3 Pin | Header | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **HX711 Load Cell Amp** | `DT` (DOUT) | `GPIO 6` | J3-9 | 24-bit ADC serial data input |
| **HX711 Load Cell Amp** | `SCK` (PD_SCK) | `GPIO 7` | J3-8 | 24-bit ADC serial clock output |
| **SSD1306 OLED (128×64)** | `SDA` | `GPIO 4` | J3-11 | I2C Data (`Wire.begin(4, 5)`, address `0x3C`) |
| **SSD1306 OLED (128×64)** | `SCL` | `GPIO 5` | J3-10 | I2C Clock (400 kHz Fast Mode) |
| **Status Alert LED** | `PWM` (Anode) | `GPIO 3` | J1-5 | Via 220 Ω series resistor to LED Anode; Cathode to `GND` |
| **UART0 Console** | `TX` / `RX` | `GPIO 21` / `GPIO 20` | J3-2 / J3-3 | 115200 8N1 interactive CLI & JSON telemetry stream |

### 4-Cell Load Cell Wheatstone Bridge Wiring

Four 3-wire (50 kg) half-bridge load cells form a full Wheatstone bridge into the HX711:
- **Top-Left (`E+`) & Bottom-Right (`E-`)**: Center signal wires connect to `E+` and `E-` excitation pins.
- **Bottom-Left (`A+`) & Top-Right (`A-`)**: Center signal wires connect to `A+` and `A-` differential sense pins.
- **Outer Loop**: Outer red/black half-bridge leads pair with adjacent corners as shown in `schematic.jpeg` so compression on all four feet sums constructively.

## Quick Start

### 1. Configure Wi-Fi & `ntfy.sh` Credentials (Optional)

For Wokwi simulation, the firmware defaults to the open `Wokwi-GUEST` network and public `http://ntfy.sh/smart-cup-stand` topic. For physical hardware deployment, copy the secrets template:

```bash
cp include/secrets.example.h include/secrets.h
```

Edit `include/secrets.h` (which is gitignored and automatically included by `include/config.h` via `__has_include("secrets.h")`):

```cpp
#define WIFI_SSID           "your-wifi-ssid"
#define WIFI_PASSWORD       "your-wifi-password"
#define NTFY_SERVER_URL     "http://ntfy.sh"
#define NTFY_DEFAULT_TOPIC  "smart-coaster-your-private-topic"
```

### 2. Build Firmware with PlatformIO

```bash
# Compile ESP32-C3 firmware (.pio/build/esp32-c3-devkitm-1/firmware.bin)
pio run -e esp32-c3-devkitm-1

# Flash to physical ESP32-C3 DevKitM-1 and open serial monitor
pio run -e esp32-c3-devkitm-1 -t upload -t monitor
```

### 3. Run in Wokwi Simulator

1. Install the **Wokwi Simulator** extension for VS Code (or Wokwi CLI).
2. Build the firmware first (`pio run -e esp32-c3-devkitm-1`).
3. Open `diagram.json` and start the simulation. Use the HX711 weight slider to simulate placing a cup (`> 30 g`), lifting (`< 20 g`), drinking (`≥ 15 g` lighter), and refilling (`≥ 30 g` heavier).

## UART CLI & Telemetry Protocol

Connect to UART0 at `115200` baud (`LF`, `CR`, or `CRLF` line endings).

### Interactive Commands

| Command | Description | Example Response |
| :--- | :--- | :--- |
| `DEMO ON` | Enable 60× time acceleration (45s reminder, 5s repeat, 1s tare) | `{"status":"OK","command":"DEMO ON","demo_mode":true}` |
| `DEMO OFF` | Restore normal production timers (45m reminder, 5m repeat, 5s tare) | `{"status":"OK","command":"DEMO OFF","demo_mode":false}` |
| `TARE` | Zero the scale at the current raw weight and clear baseline | `{"status":"OK","command":"TARE","tare_offset_g":12.4}` |
| `RESET` | Reset daily consumed intake, drink count, and active alerts | `{"status":"OK","command":"RESET","consumed_ml":0.0}` |
| `GOAL <ml>` | Set daily hydration target (`100` to `10000` ml) | `{"status":"OK","command":"GOAL","daily_goal_ml":2500.0,"progress_pct":10.0}` |
| `STATUS` | Emit full snapshot of current coaster state and metrics | `{"status":"OK","command":"STATUS","cup_state":"CUP_PRESENT",...}` |

### Single-Line JSON Telemetry Stream

Telemetry packets are emitted on every domain event (`CUP_PLACED`, `CUP_LIFTED`, `DRINK_LOGGED`, `LIFT_NO_DRINK`, `REFILL_DETECTED`, `REMINDER_ALERT`, `REPEAT_ALERT`, `AUTO_TARE_APPLIED`, `GOAL_REACHED`) and periodically every `5000 ms` (`HEARTBEAT`):

```json
{"event":"DRINK_LOGGED","time_ms":42500,"net_weight_g":280.0,"cup_state":"CUP_PRESENT","alert_state":"COUNTING","is_stable":true,"consumed_ml":120.0,"daily_goal_ml":2000.0,"intake_pct":6.0,"remind_countdown_s":2700,"demo_mode":false,"state":"CUP_PRESENT","weight_g":280.0,"goal_ml":2000.0,"countdown_s":2700,"stable":true,"demo":false}
```

## Testing

The repository includes both native C++ unit tests and a 5-tier opaque-box End-to-End (E2E) verification suite.

```bash
# 1. Run C++ native unit tests (domain state machine + pin safety invariants)
pio test -e native

# 2. Run the full 5-tier Python E2E test suite (172 tests across Tiers 1-5)
python3 test/e2e/run_e2e_tests.py

# Or run via pytest
python3 -m pytest test/e2e -v
```
