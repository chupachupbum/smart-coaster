#include <unity.h>
#include "coaster_domain.h"
#include "../../src/hal/led_ring.h"
#include "../../src/hal/led_ring.cpp"

static LedRing* ring = nullptr;

void setUp(void) {
    ring = new LedRing(PIN_LED_RING, 16, 80);
    ring->begin();
}

void tearDown(void) {
    delete ring;
    ring = nullptr;
}

void test_led_ring_initial_mode(void) {
    TEST_ASSERT_EQUAL(LedRingMode::OFF, ring->getMode());
    TEST_ASSERT_EQUAL_STRING("OFF", ring->getModeString());
    TEST_ASSERT_FALSE(ring->isWifiApActive());
}

void test_wifi_connected_flash_one_shot(void) {
    uint32_t start_ms = 1000;
    ring->triggerWifiConnected(start_ms);
    TEST_ASSERT_TRUE(ring->isTransientAnimationActive());

    // During 1000ms flash
    ring->update(start_ms + 200, CupState::EMPTY, AlertState::IDLE);
    TEST_ASSERT_EQUAL(LedRingMode::WIFI_CONNECTED_FLASH, ring->getMode());
    TEST_ASSERT_EQUAL_STRING("WIFI_CONNECTED_FLASH", ring->getModeString());
    TEST_ASSERT_TRUE(ring->getActiveLedCount() > 0);

    // After 1000ms duration, flash expires
    ring->update(start_ms + 1001, CupState::EMPTY, AlertState::IDLE);
    TEST_ASSERT_FALSE(ring->isTransientAnimationActive());
    TEST_ASSERT_EQUAL(LedRingMode::OFF, ring->getMode());
}

void test_wifi_error_pulse_timing(void) {
    uint32_t start_ms = 1000;
    ring->triggerWifiError(start_ms);
    TEST_ASSERT_TRUE(ring->isTransientAnimationActive());

    // 0..250ms: ON (magenta-red {255, 0, 80})
    ring->update(start_ms + 100, CupState::EMPTY, AlertState::IDLE);
    TEST_ASSERT_EQUAL(LedRingMode::WIFI_ERROR_PULSE, ring->getMode());
    TEST_ASSERT_EQUAL_STRING("WIFI_ERROR_PULSE", ring->getModeString());
    RgbColor c1 = ring->getPixelColor(0);
    TEST_ASSERT_EQUAL_UINT8(255, c1.r);
    TEST_ASSERT_EQUAL_UINT8(0, c1.g);
    TEST_ASSERT_EQUAL_UINT8(80, c1.b);

    // 250..500ms: OFF
    ring->update(start_ms + 300, CupState::EMPTY, AlertState::IDLE);
    RgbColor c2 = ring->getPixelColor(0);
    TEST_ASSERT_EQUAL_UINT8(0, c2.r);
    TEST_ASSERT_EQUAL_UINT8(0, c2.g);
    TEST_ASSERT_EQUAL_UINT8(0, c2.b);

    // 500..750ms: ON
    ring->update(start_ms + 600, CupState::EMPTY, AlertState::IDLE);
    RgbColor c3 = ring->getPixelColor(0);
    TEST_ASSERT_EQUAL_UINT8(255, c3.r);
    TEST_ASSERT_EQUAL_UINT8(0, c3.g);
    TEST_ASSERT_EQUAL_UINT8(80, c3.b);

    // 750..1000ms: OFF
    ring->update(start_ms + 800, CupState::EMPTY, AlertState::IDLE);
    RgbColor c4 = ring->getPixelColor(0);
    TEST_ASSERT_EQUAL_UINT8(0, c4.r);
    TEST_ASSERT_EQUAL_UINT8(0, c4.g);
    TEST_ASSERT_EQUAL_UINT8(0, c4.b);

    // > 1000ms: returns to OFF
    ring->update(start_ms + 1005, CupState::EMPTY, AlertState::IDLE);
    TEST_ASSERT_EQUAL(LedRingMode::OFF, ring->getMode());
}

void test_wifi_ap_beacon_when_empty_and_priority(void) {
    ring->setWifiApActive(true);
    TEST_ASSERT_TRUE(ring->isWifiApActive());

    // When EMPTY, renders WIFI_AP_BEACON
    ring->update(1000, CupState::EMPTY, AlertState::IDLE);
    TEST_ASSERT_EQUAL(LedRingMode::WIFI_AP_BEACON, ring->getMode());
    TEST_ASSERT_EQUAL_STRING("WIFI_AP_BEACON", ring->getModeString());
    TEST_ASSERT_TRUE(ring->getActiveLedCount() >= 2);

    // Hydration priority: when cup placed (STABILIZING), SPINNER overrides AP beacon
    ring->update(1000, CupState::STABILIZING, AlertState::IDLE);
    TEST_ASSERT_EQUAL(LedRingMode::SPINNER, ring->getMode());

    // Hydration priority: when CUP_PRESENT, AMBIENT_WARM overrides AP beacon
    ring->update(1000, CupState::CUP_PRESENT, AlertState::IDLE);
    TEST_ASSERT_EQUAL(LedRingMode::AMBIENT_WARM, ring->getMode());

    // Hydration priority: when CUP_LIFTED, LIFTED_GLOW overrides AP beacon
    ring->update(1000, CupState::CUP_LIFTED, AlertState::IDLE);
    TEST_ASSERT_EQUAL(LedRingMode::LIFTED_GLOW, ring->getMode());

    // Hydration priority: when alert active, alert overrides AP beacon
    ring->update(1000, CupState::EMPTY, AlertState::ALERT_DUE);
    TEST_ASSERT_EQUAL(LedRingMode::ALERT_BREATHING, ring->getMode());

    // Back to EMPTY and IDLE: resumes WIFI_AP_BEACON
    ring->update(1000, CupState::EMPTY, AlertState::IDLE);
    TEST_ASSERT_EQUAL(LedRingMode::WIFI_AP_BEACON, ring->getMode());

    // Disable AP mode: returns to OFF
    ring->setWifiApActive(false);
    ring->update(1000, CupState::EMPTY, AlertState::IDLE);
    TEST_ASSERT_EQUAL(LedRingMode::OFF, ring->getMode());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_led_ring_initial_mode);
    RUN_TEST(test_wifi_connected_flash_one_shot);
    RUN_TEST(test_wifi_error_pulse_timing);
    RUN_TEST(test_wifi_ap_beacon_when_empty_and_priority);
    return UNITY_END();
}
