/**
 * @file display_oled.h
 * @brief SSD1306 128x64 OLED UI driver for ESP32-C3 Smart Cup Stand.
 *
 * Implements a 3-tier visual geometry:
 * - Header Bar (y=0..12): State badge, DEMO indicator, Wi-Fi status.
 * - Main Body (y=14..45): Large net weight (size 2), stability indicator,
 *                          countdown timer or flashing alert banner.
 * - Footer Bar (y=47..63): Daily intake progress text and graphical progress bar.
 *
 * Includes frame-rate throttling (10Hz / 100ms) to protect I2C bandwidth.
 */

#ifndef HAL_DISPLAY_OLED_H_
#define HAL_DISPLAY_OLED_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

#include "coaster_domain.h"
#include "pins_config.h"

class DisplayOled {
public:
    static constexpr uint32_t DEFAULT_REFRESH_INTERVAL_MS = 100; // 10 Hz rate limit
    static constexpr uint32_t DEFAULT_SPLASH_DURATION_MS   = 3000; // 3 seconds splash

    /**
     * @brief Construct a new DisplayOled driver.
     * @param wire Pointer to TwoWire instance (defaults to &Wire).
     */
    explicit DisplayOled(TwoWire* wire = &Wire);

    /**
     * @brief Initialize I2C bus and the SSD1306 OLED hardware.
     * @param i2c_addr I2C 7-bit slave address (default OLED_I2C_ADDR = 0x3C).
     * @param wire Optional wire instance override.
     * @return true if SSD1306 responded and initialized successfully, false otherwise.
     */
    bool begin(uint8_t i2c_addr = OLED_I2C_ADDR, TwoWire* wire = &Wire);

    /**
     * @brief Check whether the OLED hardware is initialized and operational.
     */
    bool isReady() const { return is_ready_; }

    /**
     * @brief Render the 3-tier UI given current coaster domain status.
     * Throttled to 10Hz (100ms) unless force=true.
     *
     * @param status Current snapshot from CoasterDomain.
     * @param wifi_connected True if Wi-Fi client is connected.
     * @param now_ms Current system timestamp in milliseconds.
     * @param force True to bypass 10Hz throttle (e.g. on immediate state transitions).
     */
    void render(const CoasterStatus& status, bool wifi_connected, uint32_t now_ms, bool force = false);

    /**
     * @brief Trigger a celebratory transient splash banner on drink logging.
     * Displays "+X ml consumed!" feedback for the specified duration.
     *
     * @param consumed_ml Volume of water consumed in ml.
     * @param now_ms Current system timestamp in milliseconds.
     * @param duration_ms Duration to display splash banner in ms (default 3000ms).
     */
    void triggerDrinkSplash(float consumed_ml, uint32_t now_ms, uint32_t duration_ms = DEFAULT_SPLASH_DURATION_MS);

    /**
     * @brief Display initial startup / boot banner.
     * @param title Header title text.
     * @param subtitle Subtitle text.
     */
    void showBootScreen(const char* title = "SMART CUP STAND", const char* subtitle = "Initializing...");

    /**
     * @brief Clear the display buffer and flush to screen.
     */
    void clear();

    /**
     * @brief Configure the minimum interval between frame redraws.
     */
    void setRefreshIntervalMs(uint32_t interval_ms) { render_interval_ms_ = interval_ms; }
    uint32_t getRefreshIntervalMs() const { return render_interval_ms_; }

private:
    Adafruit_SSD1306 display_;
    TwoWire* wire_;
    bool is_ready_;
    uint32_t last_render_ms_;
    uint32_t render_interval_ms_;

    uint32_t splash_until_ms_;
    float splash_ml_;

    void renderHeader(const CoasterStatus& status, bool wifi_connected, uint32_t now_ms);
    void renderBody(const CoasterStatus& status, uint32_t now_ms);
    void renderFooter(const CoasterStatus& status);
    void renderSplash(uint32_t now_ms);
};

#endif // HAL_DISPLAY_OLED_H_
