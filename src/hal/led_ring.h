#ifndef HAL_LED_RING_H_
#define HAL_LED_RING_H_

#include <cstdint>
#include <cmath>
#include "pins_config.h"
#include "coaster_domain.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#endif

enum class LedRingMode : uint8_t {
    OFF = 0,
    AMBIENT_WARM,
    LIFTED_GLOW,
    SPINNER,
    INTAKE_ARC,
    ALERT_BREATHING,
    ALERT_URGENT,
    RIPPLE_DRINK,
    RIPPLE_REFILL,
    RAINBOW_GOAL,
    WIFI_AP_BEACON,
    WIFI_CONNECTED_FLASH,
    WIFI_ERROR_PULSE
};

struct RgbColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

/**
 * @brief Non-blocking 16-pixel WS2812B LED ring driver with native host pixel buffer.
 */
class LedRing {
public:
    static constexpr uint8_t  DEFAULT_PIN                = PIN_LED_RING;
    static constexpr uint16_t PIXEL_COUNT                = LED_RING_PIXEL_COUNT;
    static constexpr uint16_t MAX_PIXELS                 = (LED_RING_PIXEL_COUNT > 32) ? LED_RING_PIXEL_COUNT : 32;
    static constexpr uint8_t  DEFAULT_MAX_BRIGHTNESS     = LED_RING_MAX_BRIGHTNESS;

    static constexpr uint32_t DRINK_RIPPLE_DURATION_MS   = 1000;
    static constexpr uint32_t REFILL_RIPPLE_DURATION_MS  = 1000;
    static constexpr uint32_t GOAL_RAINBOW_DURATION_MS   = 3000;
    static constexpr uint32_t INTAKE_ARC_DURATION_MS     = 3000;
    static constexpr uint32_t BREATHING_PERIOD_NORMAL_MS = 1500;
    static constexpr uint32_t BREATHING_PERIOD_DEMO_MS   = 500;
    static constexpr uint32_t URGENT_PERIOD_NORMAL_MS    = 750;
    static constexpr uint32_t URGENT_PERIOD_DEMO_MS      = 250;
    static constexpr uint32_t SPINNER_STEP_MS            = 50;
    static constexpr uint32_t WIFI_FLASH_DURATION_MS     = 1000;
    static constexpr uint32_t AP_BEACON_STEP_MS          = 80;

    explicit LedRing(uint8_t pin = DEFAULT_PIN,
                     uint16_t num_pixels = PIXEL_COUNT,
                     uint8_t max_brightness = DEFAULT_MAX_BRIGHTNESS);

    bool begin(uint8_t pin = DEFAULT_PIN);
    void update(uint32_t now_ms, const CoasterStatus& status);
    void update(uint32_t now_ms, CupState cup_state, AlertState alert_state, float progress_pct = 0.0f);

    void triggerCupPlacedArc(uint32_t now_ms, float progress_pct);
    void triggerDrinkLogged(uint32_t now_ms, float progress_pct);
    void triggerGoalReached(uint32_t now_ms, float progress_pct = 100.0f);
    void triggerRefillDetected(uint32_t now_ms);
    void triggerWifiConnected(uint32_t now_ms);
    void triggerWifiError(uint32_t now_ms);

    void setWifiApActive(bool active);
    bool isWifiApActive() const { return wifi_ap_active_; }

    void setDemoMode(bool enable);
    bool isDemoMode() const { return demo_mode_; }
    void clear();

    LedRingMode getMode() const { return mode_; }
    const char* getModeString() const;
    uint8_t getActiveLedCount() const;
    RgbColor getPixelColor(uint16_t index) const;
    uint8_t getCurrentBrightnessLevel() const { return current_brightness_level_; }
    bool isTransientAnimationActive() const;
    bool isAlertPatternActive() const;

    static uint8_t computeBreathingIntensity(uint32_t now_ms, uint32_t period_ms);
    static uint8_t computeIntakeArcPixels(float progress_pct, uint16_t total_pixels = PIXEL_COUNT);

private:
    uint8_t  pin_;
    uint16_t num_pixels_;
    uint8_t  max_brightness_;

    bool        initialized_              = false;
    bool        demo_mode_                = false;
    bool        wifi_ap_active_           = false;
    LedRingMode mode_                     = LedRingMode::OFF;
    uint8_t     current_brightness_level_ = 0;

    bool        one_shot_active_          = false;
    LedRingMode one_shot_mode_            = LedRingMode::OFF;
    uint32_t    one_shot_start_ms_        = 0;
    uint32_t    one_shot_duration_ms_     = 0;
    bool        chain_arc_after_one_shot_ = false;
    float       pending_arc_progress_pct_ = 0.0f;

    bool        arc_preview_active_       = false;
    uint32_t    arc_start_ms_             = 0;
    float       arc_progress_pct_         = 0.0f;

    RgbColor    pixels_[MAX_PIXELS]{};

#ifdef ARDUINO
    Adafruit_NeoPixel strip_;
#endif

    static uint16_t clampPixelCount(uint16_t count);
    static RgbColor scaleColor(RgbColor base, uint8_t intensity);
    static RgbColor wheelColor(uint8_t pos);

    void fillSolid(RgbColor color);
    void renderOneShot(LedRingMode mode, uint32_t elapsed_ms);
    void renderBreathing(RgbColor base_color, uint32_t now_ms, uint32_t period_ms);
    void renderIntakeArc(float progress_pct);
    void renderSpinner(uint32_t now_ms);
    void renderApBeacon(uint32_t now_ms);
    void updateBrightnessLevel();
    void flushHardware();
};

#endif // HAL_LED_RING_H_
