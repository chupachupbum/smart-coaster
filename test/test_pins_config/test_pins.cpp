#include <unity.h>
#include "pins_config.h"

void setUp(void) {}
void tearDown(void) {}

void test_pin_assignments(void) {
    TEST_ASSERT_EQUAL_INT(3, PIN_LED_RING);
    TEST_ASSERT_EQUAL_INT(4, ESP32_CLASSIC_PIN_LED_RING);
    TEST_ASSERT_EQUAL_INT(16, LED_RING_PIXEL_COUNT);
    TEST_ASSERT_EQUAL_INT(6, PIN_HX711_DT);
    TEST_ASSERT_EQUAL_INT(7, PIN_HX711_SCK);
    TEST_ASSERT_EQUAL_INT(21, PIN_UART0_TX);
    TEST_ASSERT_EQUAL_INT(20, PIN_UART0_RX);
}

void test_calibration_and_ring_constants(void) {
    TEST_ASSERT_EQUAL_INT(16, LED_RING_PIXEL_COUNT);
    TEST_ASSERT_EQUAL_INT(80, LED_RING_MAX_BRIGHTNESS);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.42f, HX711_SCALE_FACTOR);
    TEST_ASSERT_EQUAL_INT(115200, SERIAL_BAUD_RATE);
    TEST_ASSERT_EQUAL_INT(128, HX711_DEFAULT_GAIN);
}

void test_strapping_avoidance_integrity(void) {
    const int active_pins[] = {
        PIN_LED_RING,
        PIN_HX711_DT,
        PIN_HX711_SCK,
        PIN_UART0_TX,
        PIN_UART0_RX
    };
    const size_t num_pins = sizeof(active_pins) / sizeof(active_pins[0]);

    for (size_t i = 0; i < num_pins; ++i) {
        TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO2, active_pins[i]);
        TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO8, active_pins[i]);
        TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO9, active_pins[i]);
    }
}

void test_pin_uniqueness(void) {
    const int pins[] = {
        PIN_LED_RING,
        PIN_HX711_DT,
        PIN_HX711_SCK,
        PIN_UART0_TX,
        PIN_UART0_RX
    };
    const size_t num_pins = sizeof(pins) / sizeof(pins[0]);

    for (size_t i = 0; i < num_pins; ++i) {
        for (size_t j = i + 1; j < num_pins; ++j) {
            TEST_ASSERT_NOT_EQUAL(pins[i], pins[j]);
        }
    }
}

void test_esp32_classic_pin_safety_invariants(void) {
    const int classic_pins[] = {
        ESP32_CLASSIC_PIN_LED_RING,
        ESP32_CLASSIC_PIN_HX711_DT,
        ESP32_CLASSIC_PIN_HX711_SCK,
        ESP32_CLASSIC_PIN_UART0_TX,
        ESP32_CLASSIC_PIN_UART0_RX
    };
    const size_t num_pins = sizeof(classic_pins) / sizeof(classic_pins[0]);

    for (size_t i = 0; i < num_pins; ++i) {
        for (size_t j = i + 1; j < num_pins; ++j) {
            TEST_ASSERT_NOT_EQUAL(classic_pins[i], classic_pins[j]);
        }
        TEST_ASSERT_TRUE(classic_pins[i] < ESP32_CLASSIC_FLASH_PIN_MIN ||
                         classic_pins[i] > ESP32_CLASSIC_FLASH_PIN_MAX);
        TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO0, classic_pins[i]);
        TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO2, classic_pins[i]);
        TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO12, classic_pins[i]);
        TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO15, classic_pins[i]);
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_pin_assignments);
    RUN_TEST(test_calibration_and_ring_constants);
    RUN_TEST(test_strapping_avoidance_integrity);
    RUN_TEST(test_pin_uniqueness);
    RUN_TEST(test_esp32_classic_pin_safety_invariants);
    return UNITY_END();
}
