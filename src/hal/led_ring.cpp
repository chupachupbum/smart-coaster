#include "led_ring.h"

namespace {
constexpr RgbColor kColorOff        = {0, 0, 0};
constexpr RgbColor kColorAmbient    = {28, 22, 12};
constexpr RgbColor kColorLifted     = {64, 50, 28};
constexpr RgbColor kColorAlertDue   = {255, 110, 0};
constexpr RgbColor kColorAlertUrg   = {255, 35, 0};
constexpr RgbColor kSpinnerHead     = {0, 220, 255};
constexpr RgbColor kSpinnerTail1    = {0, 110, 140};
constexpr RgbColor kSpinnerTail2    = {0, 40, 55};
constexpr RgbColor kDrinkColors[4]  = {
    {0, 255, 60},
    {0, 180, 40},
    {0, 100, 20},
    {0, 40, 10}
};
constexpr RgbColor kRefillColors[3] = {
    {0, 180, 255},
    {0, 90, 200},
    {0, 30, 100}
};
} // namespace

LedRing::LedRing(uint8_t pin, uint16_t num_pixels, uint8_t max_brightness)
    : pin_(pin),
      num_pixels_(clampPixelCount(num_pixels)),
      max_brightness_(max_brightness)
#ifdef ARDUINO
      , strip_(clampPixelCount(num_pixels), pin, NEO_GRB + NEO_KHZ800)
#endif
{
    fillSolid(kColorOff);
}

uint16_t LedRing::clampPixelCount(uint16_t count) {
    if (count == 0) return 1;
    if (count > MAX_PIXELS) return MAX_PIXELS;
    return count;
}

bool LedRing::begin(uint8_t pin) {
    pin_ = pin;
    one_shot_active_ = false;
    chain_arc_after_one_shot_ = false;
    arc_preview_active_ = false;
    wifi_ap_active_ = false;
    mode_ = LedRingMode::OFF;
    current_brightness_level_ = 0;
    fillSolid(kColorOff);

#ifdef ARDUINO
    strip_.updateLength(num_pixels_);
    strip_.setPin(pin_);
    strip_.begin();
    strip_.setBrightness(max_brightness_);
    strip_.clear();
    strip_.show();
#endif

    initialized_ = true;
    return true;
}

void LedRing::setDemoMode(bool enable) {
    demo_mode_ = enable;
}

void LedRing::clear() {
    one_shot_active_ = false;
    chain_arc_after_one_shot_ = false;
    arc_preview_active_ = false;
    mode_ = LedRingMode::OFF;
    current_brightness_level_ = 0;
    fillSolid(kColorOff);
    flushHardware();
}

void LedRing::triggerCupPlacedArc(uint32_t now_ms, float progress_pct) {
    arc_preview_active_ = true;
    arc_start_ms_ = now_ms;
    arc_progress_pct_ = progress_pct;
}

void LedRing::triggerDrinkLogged(uint32_t now_ms, float progress_pct) {
    if (one_shot_active_ &&
        one_shot_mode_ == LedRingMode::RAINBOW_GOAL &&
        one_shot_start_ms_ == now_ms) {
        pending_arc_progress_pct_ = progress_pct;
        return;
    }

    one_shot_active_ = true;
    one_shot_mode_ = LedRingMode::RIPPLE_DRINK;
    one_shot_start_ms_ = now_ms;
    one_shot_duration_ms_ = DRINK_RIPPLE_DURATION_MS;
    chain_arc_after_one_shot_ = true;
    pending_arc_progress_pct_ = progress_pct;
}

void LedRing::triggerGoalReached(uint32_t now_ms, float progress_pct) {
    one_shot_active_ = true;
    one_shot_mode_ = LedRingMode::RAINBOW_GOAL;
    one_shot_start_ms_ = now_ms;
    one_shot_duration_ms_ = GOAL_RAINBOW_DURATION_MS;
    chain_arc_after_one_shot_ = true;
    pending_arc_progress_pct_ = progress_pct;
}

void LedRing::triggerRefillDetected(uint32_t now_ms) {
    one_shot_active_ = true;
    one_shot_mode_ = LedRingMode::RIPPLE_REFILL;
    one_shot_start_ms_ = now_ms;
    one_shot_duration_ms_ = REFILL_RIPPLE_DURATION_MS;
    chain_arc_after_one_shot_ = false;
    arc_preview_active_ = false;
}

void LedRing::triggerWifiConnected(uint32_t now_ms) {
    one_shot_active_ = true;
    one_shot_mode_ = LedRingMode::WIFI_CONNECTED_FLASH;
    one_shot_start_ms_ = now_ms;
    one_shot_duration_ms_ = WIFI_FLASH_DURATION_MS;
    chain_arc_after_one_shot_ = false;
    arc_preview_active_ = false;
}

void LedRing::triggerWifiError(uint32_t now_ms) {
    one_shot_active_ = true;
    one_shot_mode_ = LedRingMode::WIFI_ERROR_PULSE;
    one_shot_start_ms_ = now_ms;
    one_shot_duration_ms_ = WIFI_FLASH_DURATION_MS;
    chain_arc_after_one_shot_ = false;
    arc_preview_active_ = false;
}

void LedRing::setWifiApActive(bool active) {
    wifi_ap_active_ = active;
}

void LedRing::update(uint32_t now_ms, const CoasterStatus& status) {
    update(now_ms, status.cup_state, status.alert_state, status.progress_pct);
}

void LedRing::update(uint32_t now_ms, CupState cup_state, AlertState alert_state, float progress_pct) {
    (void)progress_pct;

    if (one_shot_active_) {
        uint32_t elapsed = static_cast<uint32_t>(now_ms - one_shot_start_ms_);
        if (elapsed < one_shot_duration_ms_) {
            mode_ = one_shot_mode_;
            renderOneShot(one_shot_mode_, elapsed);
            updateBrightnessLevel();
            flushHardware();
            return;
        }

        one_shot_active_ = false;
        if (chain_arc_after_one_shot_) {
            chain_arc_after_one_shot_ = false;
            arc_preview_active_ = true;
            arc_start_ms_ = one_shot_start_ms_ + one_shot_duration_ms_;
            arc_progress_pct_ = pending_arc_progress_pct_;
        }
    }

    if (cup_state != CupState::CUP_PRESENT) {
        arc_preview_active_ = false;
    } else if (arc_preview_active_) {
        uint32_t arc_elapsed = static_cast<uint32_t>(now_ms - arc_start_ms_);
        if (!(arc_elapsed < INTAKE_ARC_DURATION_MS)) {
            arc_preview_active_ = false;
        }
    }

    if (alert_state == AlertState::ALERT_REPEAT) {
        mode_ = LedRingMode::ALERT_URGENT;
        uint32_t period = demo_mode_ ? URGENT_PERIOD_DEMO_MS : URGENT_PERIOD_NORMAL_MS;
        renderBreathing(kColorAlertUrg, now_ms, period);
        flushHardware();
        return;
    }

    if (alert_state == AlertState::ALERT_DUE) {
        mode_ = LedRingMode::ALERT_BREATHING;
        uint32_t period = demo_mode_ ? BREATHING_PERIOD_DEMO_MS : BREATHING_PERIOD_NORMAL_MS;
        renderBreathing(kColorAlertDue, now_ms, period);
        flushHardware();
        return;
    }

    if (arc_preview_active_ && cup_state == CupState::CUP_PRESENT) {
        mode_ = LedRingMode::INTAKE_ARC;
        renderIntakeArc(arc_progress_pct_);
        updateBrightnessLevel();
        flushHardware();
        return;
    }

    switch (cup_state) {
        case CupState::STABILIZING:
        case CupState::RETURN_STABILIZING:
            mode_ = LedRingMode::SPINNER;
            renderSpinner(now_ms);
            break;
        case CupState::CUP_PRESENT:
            mode_ = LedRingMode::AMBIENT_WARM;
            fillSolid(kColorAmbient);
            break;
        case CupState::CUP_LIFTED:
            mode_ = LedRingMode::LIFTED_GLOW;
            fillSolid(kColorLifted);
            break;
        case CupState::EMPTY:
        default:
            if (wifi_ap_active_) {
                mode_ = LedRingMode::WIFI_AP_BEACON;
                renderApBeacon(now_ms);
            } else {
                mode_ = LedRingMode::OFF;
                fillSolid(kColorOff);
            }
            break;
    }

    updateBrightnessLevel();
    flushHardware();
}

const char* LedRing::getModeString() const {
    switch (mode_) {
        case LedRingMode::OFF:             return "OFF";
        case LedRingMode::AMBIENT_WARM:    return "AMBIENT_WARM";
        case LedRingMode::LIFTED_GLOW:     return "LIFTED_GLOW";
        case LedRingMode::SPINNER:         return "SPINNER";
        case LedRingMode::INTAKE_ARC:      return "INTAKE_ARC";
        case LedRingMode::ALERT_BREATHING: return "ALERT_BREATHING";
        case LedRingMode::ALERT_URGENT:    return "ALERT_URGENT";
        case LedRingMode::RIPPLE_DRINK:         return "RIPPLE_DRINK";
        case LedRingMode::RIPPLE_REFILL:        return "RIPPLE_REFILL";
        case LedRingMode::RAINBOW_GOAL:         return "RAINBOW_GOAL";
        case LedRingMode::WIFI_AP_BEACON:       return "WIFI_AP_BEACON";
        case LedRingMode::WIFI_CONNECTED_FLASH: return "WIFI_CONNECTED_FLASH";
        case LedRingMode::WIFI_ERROR_PULSE:     return "WIFI_ERROR_PULSE";
        default:                                return "OFF";
    }
}

uint8_t LedRing::getActiveLedCount() const {
    uint8_t active = 0;
    for (uint16_t i = 0; i < num_pixels_; ++i) {
        if (pixels_[i].r > 0 || pixels_[i].g > 0 || pixels_[i].b > 0) {
            ++active;
        }
    }
    return active;
}

RgbColor LedRing::getPixelColor(uint16_t index) const {
    if (index >= num_pixels_) {
        return kColorOff;
    }
    return pixels_[index];
}

bool LedRing::isTransientAnimationActive() const {
    return one_shot_active_ || arc_preview_active_;
}

bool LedRing::isAlertPatternActive() const {
    return mode_ == LedRingMode::ALERT_BREATHING || mode_ == LedRingMode::ALERT_URGENT;
}

uint8_t LedRing::computeBreathingIntensity(uint32_t now_ms, uint32_t period_ms) {
    if (period_ms == 0) return 0;
    uint32_t phase_ms = now_ms % period_ms;
    float angle = 3.14159265358979323846f * (static_cast<float>(phase_ms) / static_cast<float>(period_ms));
    float s = std::sin(angle);
    float intensity = s * s;
    return static_cast<uint8_t>(std::round(intensity * 255.0f));
}

uint8_t LedRing::computeIntakeArcPixels(float progress_pct, uint16_t total_pixels) {
    if (total_pixels == 0) return 0;
    float clamped = (std::isnan(progress_pct) || progress_pct < 0.0f) ? 0.0f : progress_pct;
    if (clamped > 100.0f) clamped = 100.0f;

    long count = std::lround((clamped / 100.0f) * static_cast<float>(total_pixels));
    if (count < 1) count = 1;
    uint16_t max_out = (total_pixels > 255) ? 255 : total_pixels;
    if (count > static_cast<long>(max_out)) count = static_cast<long>(max_out);
    return static_cast<uint8_t>(count);
}

RgbColor LedRing::scaleColor(RgbColor base, uint8_t intensity) {
    return RgbColor{
        static_cast<uint8_t>((static_cast<uint16_t>(base.r) * intensity) / 255U),
        static_cast<uint8_t>((static_cast<uint16_t>(base.g) * intensity) / 255U),
        static_cast<uint8_t>((static_cast<uint16_t>(base.b) * intensity) / 255U)
    };
}

RgbColor LedRing::wheelColor(uint8_t pos) {
    if (pos < 85) {
        return RgbColor{
            static_cast<uint8_t>(pos * 3U),
            static_cast<uint8_t>(255U - pos * 3U),
            0
        };
    }
    if (pos < 170) {
        pos -= 85;
        return RgbColor{
            static_cast<uint8_t>(255U - pos * 3U),
            0,
            static_cast<uint8_t>(pos * 3U)
        };
    }
    pos -= 170;
    return RgbColor{
        0,
        static_cast<uint8_t>(pos * 3U),
        static_cast<uint8_t>(255U - pos * 3U)
    };
}

void LedRing::fillSolid(RgbColor color) {
    for (uint16_t i = 0; i < MAX_PIXELS; ++i) {
        pixels_[i] = (i < num_pixels_) ? color : kColorOff;
    }
}

void LedRing::renderOneShot(LedRingMode mode, uint32_t elapsed_ms) {
    if (mode == LedRingMode::RAINBOW_GOAL) {
        uint16_t base_hue = static_cast<uint16_t>((elapsed_ms * 3UL * 256UL) / GOAL_RAINBOW_DURATION_MS);
        for (uint16_t i = 0; i < num_pixels_; ++i) {
            uint8_t hue = static_cast<uint8_t>((base_hue + (i * 256U) / num_pixels_) & 0xFFU);
            pixels_[i] = wheelColor(hue);
        }
        return;
    }

    fillSolid(kColorOff);

    if (mode == LedRingMode::RIPPLE_DRINK) {
        uint16_t head = static_cast<uint16_t>(((elapsed_ms * num_pixels_) / DRINK_RIPPLE_DURATION_MS) % num_pixels_);
        uint16_t trail = (num_pixels_ < 4) ? num_pixels_ : 4;
        for (uint16_t t = 0; t < trail; ++t) {
            uint16_t idx = static_cast<uint16_t>((head + num_pixels_ - t) % num_pixels_);
            pixels_[idx] = kDrinkColors[t];
        }
        return;
    }

    if (mode == LedRingMode::RIPPLE_REFILL) {
        uint16_t half_ring = num_pixels_ / 2;
        uint16_t step = static_cast<uint16_t>((elapsed_ms * (half_ring + 1)) / REFILL_RIPPLE_DURATION_MS);
        if (step > half_ring) step = half_ring;
        for (uint16_t i = 0; i < num_pixels_; ++i) {
            uint16_t cw = i;
            uint16_t ccw = static_cast<uint16_t>(num_pixels_ - i);
            uint16_t dist = (cw < ccw) ? cw : ccw;
            if (dist <= step && (step - dist) < 3) {
                pixels_[i] = kRefillColors[step - dist];
            }
        }
        return;
    }

    if (mode == LedRingMode::WIFI_CONNECTED_FLASH) {
        constexpr RgbColor kConnectedColor = {0, 240, 180};
        uint8_t intensity = computeBreathingIntensity(elapsed_ms, WIFI_FLASH_DURATION_MS);
        if (intensity < 40 && elapsed_ms < WIFI_FLASH_DURATION_MS) {
            intensity = 40 + static_cast<uint8_t>((intensity * (255 - 40)) / 255);
        }
        fillSolid(scaleColor(kConnectedColor, intensity));
        return;
    }

    if (mode == LedRingMode::WIFI_ERROR_PULSE) {
        constexpr RgbColor kWifiErrorColor = {255, 0, 80};
        if ((elapsed_ms < 250) || (elapsed_ms >= 500 && elapsed_ms < 750)) {
            fillSolid(kWifiErrorColor);
        } else {
            fillSolid(kColorOff);
        }
        return;
    }
}

void LedRing::renderBreathing(RgbColor base_color, uint32_t now_ms, uint32_t period_ms) {
    uint8_t intensity = computeBreathingIntensity(now_ms, period_ms);
    current_brightness_level_ = intensity;
    fillSolid(scaleColor(base_color, intensity));
}

void LedRing::renderIntakeArc(float progress_pct) {
    fillSolid(kColorOff);
    uint8_t active_n = computeIntakeArcPixels(progress_pct, num_pixels_);
    for (uint16_t i = 0; i < active_n; ++i) {
        if (num_pixels_ > 1) {
            float frac = static_cast<float>(i) / static_cast<float>(num_pixels_ - 1);
            pixels_[i] = RgbColor{
                0,
                static_cast<uint8_t>(std::lround(180.0f + 75.0f * frac)),
                static_cast<uint8_t>(std::lround(220.0f - 140.0f * frac))
            };
        } else {
            pixels_[i] = RgbColor{0, 220, 160};
        }
    }
}

void LedRing::renderSpinner(uint32_t now_ms) {
    fillSolid(kColorOff);
    uint16_t head = static_cast<uint16_t>((now_ms / SPINNER_STEP_MS) % num_pixels_);
    pixels_[head] = kSpinnerHead;
    if (num_pixels_ >= 2) {
        pixels_[(head + num_pixels_ - 1) % num_pixels_] = kSpinnerTail1;
    }
    if (num_pixels_ >= 3) {
        pixels_[(head + num_pixels_ - 2) % num_pixels_] = kSpinnerTail2;
    }
}

void LedRing::renderApBeacon(uint32_t now_ms) {
    fillSolid(kColorOff);
    if (num_pixels_ == 0) return;
    uint16_t head1 = static_cast<uint16_t>((now_ms / AP_BEACON_STEP_MS) % num_pixels_);
    uint16_t head2 = static_cast<uint16_t>((head1 + num_pixels_ / 2) % num_pixels_);

    constexpr RgbColor kBeacon1 = {30, 90, 255};
    constexpr RgbColor kBeacon2 = {130, 40, 255};

    pixels_[head1] = kBeacon1;
    pixels_[head2] = kBeacon2;

    if (num_pixels_ >= 4) {
        uint16_t t1 = static_cast<uint16_t>((head1 + num_pixels_ - 1) % num_pixels_);
        uint16_t t2 = static_cast<uint16_t>((head2 + num_pixels_ - 1) % num_pixels_);
        if (t1 != head2) pixels_[t1] = scaleColor(kBeacon1, 90);
        if (t2 != head1) pixels_[t2] = scaleColor(kBeacon2, 90);
    }
    if (num_pixels_ >= 6) {
        uint16_t t1 = static_cast<uint16_t>((head1 + num_pixels_ - 2) % num_pixels_);
        uint16_t t2 = static_cast<uint16_t>((head2 + num_pixels_ - 2) % num_pixels_);
        if (t1 != head2 && t1 != head1) pixels_[t1] = scaleColor(kBeacon1, 30);
        if (t2 != head1 && t2 != head2) pixels_[t2] = scaleColor(kBeacon2, 30);
    }
}

void LedRing::updateBrightnessLevel() {
    uint8_t max_ch = 0;
    for (uint16_t i = 0; i < num_pixels_; ++i) {
        if (pixels_[i].r > max_ch) max_ch = pixels_[i].r;
        if (pixels_[i].g > max_ch) max_ch = pixels_[i].g;
        if (pixels_[i].b > max_ch) max_ch = pixels_[i].b;
    }
    current_brightness_level_ = max_ch;
}

void LedRing::flushHardware() {
#ifdef ARDUINO
    if (!initialized_) return;
    for (uint16_t i = 0; i < num_pixels_; ++i) {
        strip_.setPixelColor(i, strip_.Color(pixels_[i].r, pixels_[i].g, pixels_[i].b));
    }
    strip_.show();
#endif
}
