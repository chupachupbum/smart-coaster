/**
 * @file display_oled.cpp
 * @brief SSD1306 128x64 OLED UI driver implementation for ESP32-C3 Smart Cup Stand.
 */

#include "display_oled.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

DisplayOled::DisplayOled(TwoWire* wire)
    : display_(OLED_SCREEN_WIDTH, OLED_SCREEN_HEIGHT, wire, OLED_RESET_PIN),
      wire_(wire),
      is_ready_(false),
      last_render_ms_(0),
      render_interval_ms_(DEFAULT_REFRESH_INTERVAL_MS),
      splash_until_ms_(0),
      splash_ml_(0.0f) {}

bool DisplayOled::begin(uint8_t i2c_addr, TwoWire* wire) {
    if (wire != nullptr) {
        wire_ = wire;
    }
    if (wire_ != nullptr) {
        // Set 400kHz fast I2C clock to minimize framebuffer transmission time (~23ms)
        wire_->setClock(400000);
    }

    if (!display_.begin(SSD1306_SWITCHCAPVCC, i2c_addr)) {
        is_ready_ = false;
        return false;
    }

    is_ready_ = true;
    display_.clearDisplay();
    display_.setTextColor(SSD1306_WHITE);
    display_.setTextWrap(false);
    display_.display();
    return true;
}

void DisplayOled::showBootScreen(const char* title, const char* subtitle) {
    if (!is_ready_) return;

    display_.clearDisplay();
    display_.setTextWrap(false);

    // Decorative outer border
    display_.drawRect(0, 0, 128, 64, SSD1306_WHITE);

    // Header title
    display_.setTextSize(1);
    display_.setTextColor(SSD1306_WHITE);
    int title_len = (int)strlen(title);
    int title_x = (128 - title_len * 6) / 2;
    display_.setCursor(title_x > 2 ? title_x : 4, 10);
    display_.print(title);

    // Dividing rule
    display_.drawFastHLine(10, 24, 108, SSD1306_WHITE);

    // Subtitle
    int sub_len = (int)strlen(subtitle);
    int sub_x = (128 - sub_len * 6) / 2;
    display_.setCursor(sub_x > 2 ? sub_x : 4, 32);
    display_.print(subtitle);

    // Bottom badge
    display_.setCursor(18, 48);
    display_.print(F(BOARD_OLED_BADGE));

    display_.display();
}

void DisplayOled::clear() {
    if (!is_ready_) return;
    display_.clearDisplay();
    display_.display();
}

void DisplayOled::triggerDrinkSplash(float consumed_ml, uint32_t now_ms, uint32_t duration_ms) {
    splash_ml_ = consumed_ml;
    splash_until_ms_ = now_ms + duration_ms;
}

void DisplayOled::render(const CoasterStatus& status, bool wifi_connected, uint32_t now_ms, bool force) {
    if (!is_ready_) return;

    // Frame-rate throttling to 10Hz unless force requested
    if (!force && (now_ms - last_render_ms_ < render_interval_ms_)) {
        return;
    }
    last_render_ms_ = now_ms;

    display_.clearDisplay();
    display_.setTextWrap(false);

    // Zone 1: Header Bar (y=0..12)
    renderHeader(status, wifi_connected, now_ms);

    // Divider Line 1
    display_.drawFastHLine(0, 13, 128, SSD1306_WHITE);

    // Zone 2: Main Area (y=14..45)
    if (now_ms < splash_until_ms_) {
        renderSplash(now_ms);
    } else {
        renderBody(status, now_ms);
    }

    // Divider Line 2
    display_.drawFastHLine(0, 46, 128, SSD1306_WHITE);

    // Zone 3: Footer Bar (y=47..63)
    renderFooter(status);

    display_.display();
}

void DisplayOled::renderHeader(const CoasterStatus& status, bool wifi_connected, uint32_t now_ms) {
    display_.setTextSize(1);

    // 1. Left State Badge
    if (now_ms < splash_until_ms_) {
        display_.fillRect(0, 1, 48, 11, SSD1306_WHITE);
        display_.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display_.setCursor(1, 3);
        display_.print(F("[LOGGED]"));
        display_.setTextColor(SSD1306_WHITE);
    } else if (status.alert_active) {
        bool flash = (now_ms / 350) % 2 == 0;
        if (flash) {
            display_.fillRect(0, 1, 44, 11, SSD1306_WHITE);
            display_.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
            display_.setCursor(1, 3);
            display_.print(F("[ALERT]"));
            display_.setTextColor(SSD1306_WHITE);
        } else {
            display_.setCursor(0, 3);
            display_.print(F("[ALERT]"));
        }
    } else {
        const char* badge = "[EMPTY]";
        switch (status.cup_state) {
            case CupState::EMPTY:              badge = "[EMPTY]"; break;
            case CupState::STABILIZING:        badge = "[WAIT]";  break;
            case CupState::CUP_PRESENT:        badge = "[CUP]";   break;
            case CupState::CUP_LIFTED:         badge = "[LIFT]";  break;
            case CupState::RETURN_STABILIZING: badge = "[WAIT]";  break;
            default:                           badge = "[EMPTY]"; break;
        }
        display_.setCursor(0, 3);
        display_.print(badge);
    }

    // 2. Middle DEMO Badge
    if (status.demo_mode) {
        display_.setCursor(54, 3);
        display_.print(F("DEMO"));
    }

    // 3. Right Wi-Fi Indicator
    display_.setCursor(86, 3);
    if (wifi_connected) {
        display_.print(F("WIFI:OK"));
    } else {
        display_.print(F("WIFI:--"));
    }
}

void DisplayOled::renderBody(const CoasterStatus& status, uint32_t now_ms) {
    // --- Line 1: Large Net Weight & Stability (y = 16..31) ---
    char wbuf[16];
    float w = status.net_weight_g;
    if (status.cup_state == CupState::EMPTY && fabsf(w) < 0.5f) {
        w = 0.0f;
    }

    if (fabsf(w) >= 1000.0f) {
        snprintf(wbuf, sizeof(wbuf), "%.0f g", w);
    } else {
        snprintf(wbuf, sizeof(wbuf), "%.1f g", w);
    }

    display_.setTextSize(2);
    display_.setTextColor(SSD1306_WHITE);
    display_.setCursor(2, 16);
    display_.print(wbuf);

    // Stability Badge at x = 90, y = 20 (size 1)
    display_.setTextSize(1);
    display_.setCursor(90, 20);
    if (status.is_stable) {
        display_.print(F("[STBL]"));
    } else {
        display_.print(F("[WAIT]"));
    }

    // --- Line 2: Contextual Message / Alert / Countdown (y = 35) ---
    display_.setTextSize(1);
    if (status.alert_active) {
        // High-visibility flashing banner
        bool flash = (now_ms / 300) % 2 == 0;
        if (flash) {
            display_.fillRect(0, 33, 128, 12, SSD1306_WHITE);
            display_.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
            display_.setCursor(4, 35);
            display_.print(F(" * TIME TO DRINK! * "));
            display_.setTextColor(SSD1306_WHITE);
        } else {
            display_.setCursor(4, 35);
            display_.print(F(" * TIME TO DRINK! * "));
        }
    } else {
        char cbuf[24];
        switch (status.cup_state) {
            case CupState::CUP_PRESENT: {
                uint32_t secs = status.countdown_s;
                if (status.demo_mode) {
                    snprintf(cbuf, sizeof(cbuf), "Next drink: %lus", (unsigned long)secs);
                } else {
                    uint32_t mins = secs / 60;
                    uint32_t rem_s = secs % 60;
                    snprintf(cbuf, sizeof(cbuf), "Next drink: %02lu:%02lu", (unsigned long)mins, (unsigned long)rem_s);
                }
                display_.setCursor(4, 35);
                display_.print(cbuf);
                break;
            }
            case CupState::CUP_LIFTED:
                display_.setCursor(8, 35);
                display_.print(F(">> Taking a drink <<"));
                break;
            case CupState::STABILIZING:
            case CupState::RETURN_STABILIZING:
                display_.setCursor(6, 35);
                display_.print(F("Stabilizing scale..."));
                break;
            case CupState::EMPTY:
            default:
                display_.setCursor(8, 35);
                display_.print(F("Waiting for cup...  "));
                break;
        }
    }
}

void DisplayOled::renderSplash(uint32_t now_ms) {
    (void)now_ms;
    char splash_buf[24];
    snprintf(splash_buf, sizeof(splash_buf), "+%.0f ml logged!", splash_ml_);
    int len = (int)strlen(splash_buf);
    int x = (128 - len * 6) / 2;

    display_.setTextSize(1);
    display_.setTextColor(SSD1306_WHITE);
    display_.setCursor(x > 0 ? x : 4, 18);
    display_.print(splash_buf);

    display_.setCursor(14, 33);
    display_.print(F("Great job! Drink on!"));
}

void DisplayOled::renderFooter(const CoasterStatus& status) {
    display_.setTextSize(1);
    display_.setTextColor(SSD1306_WHITE);

    // Line 1: Daily progress text (e.g. "850/2000 ml  42%")
    char prog_buf[24];
    snprintf(prog_buf, sizeof(prog_buf), "%u/%u ml  %u%%",
             (unsigned)status.consumed_today_ml,
             (unsigned)status.daily_goal_ml,
             (unsigned)status.progress_pct);

    int len = (int)strlen(prog_buf);
    int x = (128 - len * 6) / 2;
    display_.setCursor(x > 0 ? x : 2, 48);
    display_.print(prog_buf);

    // Line 2: Horizontal graphic progress bar (outer box at y=57..62)
    display_.drawRect(2, 57, 124, 6, SSD1306_WHITE);

    float pct = status.progress_pct;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    int fill_w = (int)((pct / 100.0f) * 120.0f);
    if (fill_w > 0) {
        display_.fillRect(4, 59, fill_w, 2, SSD1306_WHITE);
    }
}
