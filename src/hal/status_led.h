#ifndef HAL_STATUS_LED_H_
#define HAL_STATUS_LED_H_

#include <cstdint>
#include <cmath>
#include "pins_config.h"
#include "coaster_domain.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <esp_arduino_version.h>

#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
#define STATUS_LED_USE_LEDC_V3 1
#else
#define STATUS_LED_USE_LEDC_V2 1
#endif
#endif // ARDUINO

/**
 * @brief Non-blocking hardware status LED PWM breathing and pattern driver.
 * 
 * Supports:
 * - Sinusoidal breathing with quadratic gamma correction during hydration reminder alerts.
 * - Dedicated visual patterns for EMPTY, STABILIZING, CUP_PRESENT, CUP_LIFTED, and DRINK_LOGGED.
 * - Dual-core API compatibility across Arduino-ESP32 v2.x and v3.x.
 * - Hardware abstraction allowing native host execution and unit testing without hardware mocks.
 */
class StatusLed {
public:
    // --- Hardware Configuration Defaults ---
    static constexpr uint8_t  DEFAULT_PIN             = PIN_STATUS_LED;        // GPIO 3
    static constexpr uint8_t  DEFAULT_CHANNEL         = LEDC_PWM_CHANNEL;      // Channel 0
    static constexpr uint32_t DEFAULT_FREQ_HZ         = LEDC_PWM_FREQ_HZ;      // 5000 Hz
    static constexpr uint8_t  DEFAULT_RESOLUTION_BITS = LEDC_PWM_RESOLUTION;   // 8-bit (0..255)

    // --- State Pattern Timing and Duty Cycle Constants ---
    static constexpr uint32_t BREATHING_PERIOD_NORMAL_MS = 1500;  // 1.5s per breath
    static constexpr uint32_t BREATHING_PERIOD_DEMO_MS   = 500;   // 0.5s per breath (accelerated)

    static constexpr uint32_t STABILIZING_PERIOD_MS   = 1000;     // 1.0 Hz
    static constexpr uint32_t STABILIZING_ON_TIME_MS  = 100;      // 100ms ON / 900ms OFF
    static constexpr uint8_t  STABILIZING_DUTY        = 50;       // ~20% duty cycle

    static constexpr uint8_t  CUP_PRESENT_DUTY        = 8;        // ~3% ambient session glow
    static constexpr uint8_t  CUP_LIFTED_DUTY         = 38;       // ~15% medium active glow
    static constexpr uint8_t  OFF_DUTY                = 0;        // 0% duty (idle/empty)

    static constexpr uint32_t FLASH_PULSE_DURATION_MS = 80;       // 80ms ON, 80ms OFF per pulse
    static constexpr uint32_t FLASH_TOTAL_PULSES      = 3;        // Triple flash celebration
    static constexpr uint32_t FLASH_TOTAL_DURATION_MS = FLASH_PULSE_DURATION_MS * 2 * FLASH_TOTAL_PULSES; // 480ms
    static constexpr uint8_t  FLASH_DUTY              = 255;      // 100% celebratory pulse

    /**
     * @brief Construct a new StatusLed driver instance.
     * @param pin GPIO pin number (default: PIN_STATUS_LED = 3)
     * @param channel LEDC PWM channel (default: LEDC_PWM_CHANNEL = 0)
     * @param freq_hz PWM frequency in Hz (default: LEDC_PWM_FREQ_HZ = 5000)
     * @param resolution_bits PWM resolution in bits (default: LEDC_PWM_RESOLUTION = 8)
     */
    explicit StatusLed(uint8_t pin = DEFAULT_PIN,
                       uint8_t channel = DEFAULT_CHANNEL,
                       uint32_t freq_hz = DEFAULT_FREQ_HZ,
                       uint8_t resolution_bits = DEFAULT_RESOLUTION_BITS);

    /**
     * @brief Initialize LEDC PWM peripheral and attach to output pin.
     * @param pin GPIO pin number (default: PIN_STATUS_LED = 3)
     */
    void begin(uint8_t pin = DEFAULT_PIN);

    /**
     * @brief Full parameter initialization of LEDC PWM peripheral.
     */
    void begin(uint8_t pin, uint8_t channel, uint32_t freq_hz, uint8_t resolution_bits);

    /**
     * @brief Non-blocking pattern update called from cooperative loop (~50Hz / 20ms).
     * @param now_ms Current system timestamp in milliseconds (e.g. millis())
     * @param cup_state Current domain CupState
     * @param alert_state Current domain AlertState
     * @param flash_trigger Set true on frame when DRINK_LOGGED event occurs
     */
    void update(uint32_t now_ms, CupState cup_state, AlertState alert_state, bool flash_trigger = false);

    /**
     * @brief Trigger the rapid triple-flash celebration pulse immediately.
     * @param now_ms Current system timestamp in milliseconds (0 = use last known)
     */
    void triggerDrinkLoggedFlash(uint32_t now_ms = 0);

    /**
     * @brief Enable or disable DEMO mode time acceleration for breathing animation.
     * @param enable true = 500ms period; false = 1500ms period
     */
    void setDemoMode(bool enable);

    /**
     * @brief Check whether DEMO mode breathing is enabled.
     */
    bool isDemoMode() const { return demo_mode_; }

    /**
     * @brief Get current 8-bit PWM duty cycle (0..255).
     */
    uint8_t getDuty() const { return current_duty_; }

    /**
     * @brief Check if celebration flash sequence is currently playing.
     */
    bool isFlashActive() const { return flash_active_; }

    /**
     * @brief Manually override hardware PWM duty cycle (for diagnostics/testing).
     * @param duty 8-bit duty cycle (0..255)
     */
    void setDuty(uint8_t duty);

    /**
     * @brief Compute sinusoidal gamma-corrected breathing duty cycle.
     * Mathematical formula: I = sin^2(pi * (now_ms % period_ms) / period_ms) * 255.
     * @param now_ms Timestamp in milliseconds
     * @param period_ms Breathing cycle period in milliseconds
     * @return uint8_t Calculated duty cycle in range [0, 255]
     */
    static uint8_t computeBreathingDuty(uint32_t now_ms, uint32_t period_ms);

    /**
     * @brief Compute target duty cycle for steady or cyclic states (excluding flash).
     */
    static uint8_t computeStateDuty(uint32_t now_ms, CupState cup_state, AlertState alert_state, bool demo_mode);

private:
    uint8_t  pin_;
    uint8_t  channel_;
    uint32_t freq_hz_;
    uint8_t  resolution_bits_;

    bool     initialized_ = false;
    bool     demo_mode_   = false;
    uint8_t  current_duty_ = 0;

    bool     flash_active_   = false;
    uint32_t flash_start_ms_ = 0;

    void writeHardwareDuty(uint8_t duty);
};

#endif // HAL_STATUS_LED_H_
