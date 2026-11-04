#include "status_led.h"

StatusLed::StatusLed(uint8_t pin, uint8_t channel, uint32_t freq_hz, uint8_t resolution_bits)
    : pin_(pin),
      channel_(channel),
      freq_hz_(freq_hz),
      resolution_bits_(resolution_bits),
      initialized_(false),
      demo_mode_(false),
      current_duty_(0),
      flash_active_(false),
      flash_start_ms_(0) {
}

void StatusLed::begin(uint8_t pin) {
    begin(pin, channel_, freq_hz_, resolution_bits_);
}

void StatusLed::begin(uint8_t pin, uint8_t channel, uint32_t freq_hz, uint8_t resolution_bits) {
    pin_ = pin;
    channel_ = channel;
    freq_hz_ = freq_hz;
    resolution_bits_ = resolution_bits;

#if defined(STATUS_LED_USE_LEDC_V3)
    ledcAttach(pin_, freq_hz_, resolution_bits_);
    ledcWrite(pin_, 0);
#elif defined(STATUS_LED_USE_LEDC_V2)
    ledcSetup(channel_, freq_hz_, resolution_bits_);
    ledcAttachPin(pin_, channel_);
    ledcWrite(channel_, 0);
#else
    // Host unit testing or non-Arduino environment (safe no-op)
#endif

    current_duty_ = 0;
    initialized_ = true;
}

void StatusLed::setDemoMode(bool enable) {
    demo_mode_ = enable;
}

void StatusLed::triggerDrinkLoggedFlash(uint32_t now_ms) {
    flash_active_ = true;
    flash_start_ms_ = now_ms;
}

void StatusLed::setDuty(uint8_t duty) {
    writeHardwareDuty(duty);
}

void StatusLed::update(uint32_t now_ms, CupState cup_state, AlertState alert_state, bool flash_trigger) {
    if (flash_trigger) {
        triggerDrinkLoggedFlash(now_ms);
    }

    uint8_t target_duty = 0;

    // 1. Highest priority: Transient celebration triple flash
    if (flash_active_) {
        uint32_t elapsed = now_ms - flash_start_ms_;
        if (elapsed < FLASH_TOTAL_DURATION_MS) {
            uint32_t pulse_phase = elapsed % (FLASH_PULSE_DURATION_MS * 2);
            target_duty = (pulse_phase < FLASH_PULSE_DURATION_MS) ? FLASH_DUTY : OFF_DUTY;
            writeHardwareDuty(target_duty);
            return;
        } else {
            flash_active_ = false;
        }
    }

    // 2. Underlying system state pattern
    target_duty = computeStateDuty(now_ms, cup_state, alert_state, demo_mode_);
    writeHardwareDuty(target_duty);
}

uint8_t StatusLed::computeBreathingDuty(uint32_t now_ms, uint32_t period_ms) {
    if (period_ms == 0) return 0;
    uint32_t phase_ms = now_ms % period_ms;
    // Symmetrical sine breathing with natural quadratic gamma correction:
    // angle = pi * (phase_ms / period_ms)
    // s = sin(angle)
    // intensity = s^2 (automatically applies gamma curve: gentle at base, rounded at peak)
    float angle = 3.14159265358979323846f * (static_cast<float>(phase_ms) / static_cast<float>(period_ms));
    float s = std::sin(angle);
    float intensity = s * s;
    return static_cast<uint8_t>(std::round(intensity * 255.0f));
}

uint8_t StatusLed::computeStateDuty(uint32_t now_ms, CupState cup_state, AlertState alert_state, bool demo_mode) {
    // Alert has priority over standard resting cup states
    if (alert_state == AlertState::ALERT_DUE || alert_state == AlertState::ALERT_REPEAT) {
        uint32_t period = demo_mode ? BREATHING_PERIOD_DEMO_MS : BREATHING_PERIOD_NORMAL_MS;
        return computeBreathingDuty(now_ms, period);
    }

    // Settling state blink: 1.0Hz (100ms ON / 900ms OFF)
    if (cup_state == CupState::STABILIZING || cup_state == CupState::RETURN_STABILIZING) {
        uint32_t phase = now_ms % STABILIZING_PERIOD_MS;
        return (phase < STABILIZING_ON_TIME_MS) ? STABILIZING_DUTY : OFF_DUTY;
    }

    // Cup resting stably: low ambient glow (~3% duty)
    if (cup_state == CupState::CUP_PRESENT) {
        return CUP_PRESENT_DUTY;
    }

    // Cup removed for drinking: medium active glow (~15% duty)
    if (cup_state == CupState::CUP_LIFTED) {
        return CUP_LIFTED_DUTY;
    }

    // Default: Empty stand or idle
    return OFF_DUTY;
}

void StatusLed::writeHardwareDuty(uint8_t duty) {
    if (!initialized_) return;
    if (duty == current_duty_) return; // Filter redundant register writes

    current_duty_ = duty;

#if defined(STATUS_LED_USE_LEDC_V3)
    ledcWrite(pin_, current_duty_);
#elif defined(STATUS_LED_USE_LEDC_V2)
    ledcWrite(channel_, current_duty_);
#else
    // Host simulation / unit test mock
#endif
}
