#include <unity.h>
#include "pins_config.h"

void setUp(void) {}
void tearDown(void) {}

void test_pin_assignments(void) {
    TEST_ASSERT_EQUAL_INT(4, PIN_OLED_SDA);
    TEST_ASSERT_EQUAL_INT(5, PIN_OLED_SCL);
    TEST_ASSERT_EQUAL_INT(6, PIN_HX711_DT);
    TEST_ASSERT_EQUAL_INT(7, PIN_HX711_SCK);
    TEST_ASSERT_EQUAL_INT(3, PIN_STATUS_LED);
    TEST_ASSERT_EQUAL_INT(21, PIN_UART0_TX);
    TEST_ASSERT_EQUAL_INT(20, PIN_UART0_RX);
}

void test_calibration_and_i2c_constants(void) {
    TEST_ASSERT_EQUAL_HEX8(0x3C, OLED_I2C_ADDR);
    TEST_ASSERT_EQUAL_INT(128, OLED_SCREEN_WIDTH);
    TEST_ASSERT_EQUAL_INT(64, OLED_SCREEN_HEIGHT);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.42f, HX711_SCALE_FACTOR);
    TEST_ASSERT_EQUAL_INT(115200, SERIAL_BAUD_RATE);
    TEST_ASSERT_EQUAL_INT(128, HX711_DEFAULT_GAIN);
    TEST_ASSERT_EQUAL_INT(0, LEDC_PWM_CHANNEL);
    TEST_ASSERT_EQUAL_INT(5000, LEDC_PWM_FREQ_HZ);
    TEST_ASSERT_EQUAL_INT(8, LEDC_PWM_RESOLUTION);
}

void test_strapping_avoidance_integrity(void) {
    // Assert no peripheral is assigned to GPIO 2
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO2, PIN_OLED_SDA);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO2, PIN_OLED_SCL);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO2, PIN_HX711_DT);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO2, PIN_HX711_SCK);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO2, PIN_STATUS_LED);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO2, PIN_UART0_TX);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO2, PIN_UART0_RX);

    // Assert no peripheral is assigned to GPIO 8 (Onboard WS2812 RGB LED)
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO8, PIN_OLED_SDA);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO8, PIN_OLED_SCL);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO8, PIN_HX711_DT);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO8, PIN_HX711_SCK);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO8, PIN_STATUS_LED);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO8, PIN_UART0_TX);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO8, PIN_UART0_RX);

    // Assert no peripheral is assigned to GPIO 9 (BOOT button strapping pin)
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO9, PIN_OLED_SDA);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO9, PIN_OLED_SCL);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO9, PIN_HX711_DT);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO9, PIN_HX711_SCK);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO9, PIN_STATUS_LED);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO9, PIN_UART0_TX);
    TEST_ASSERT_NOT_EQUAL(PIN_STRAP_GPIO9, PIN_UART0_RX);
}

void test_pin_uniqueness(void) {
    const int pins[] = {
        PIN_OLED_SDA,
        PIN_OLED_SCL,
        PIN_HX711_DT,
        PIN_HX711_SCK,
        PIN_STATUS_LED,
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
        ESP32_CLASSIC_PIN_OLED_SDA,
        ESP32_CLASSIC_PIN_OLED_SCL,
        ESP32_CLASSIC_PIN_HX711_DT,
        ESP32_CLASSIC_PIN_HX711_SCK,
        ESP32_CLASSIC_PIN_STATUS_LED,
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
    RUN_TEST(test_calibration_and_i2c_constants);
    RUN_TEST(test_strapping_avoidance_integrity);
    RUN_TEST(test_pin_uniqueness);
    RUN_TEST(test_esp32_classic_pin_safety_invariants);
    return UNITY_END();
}

