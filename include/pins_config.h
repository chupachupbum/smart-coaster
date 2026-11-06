#ifndef PINS_CONFIG_H_
#define PINS_CONFIG_H_

#include <stdint.h>

// ============================================================================
// Multi-Target Hardware Pinout & Peripheral Configuration
// Supported Profiles:
//   1. ESP32-C3 DevKitM-1 (default / Wokwi simulation)
//   2. Classic ESP32 DevKit / ESP32-D0WD-V3 (BOARD_ESP32_CLASSIC / esp32dev)
// ============================================================================

// Reference Constants for Classic ESP32 (ESP32-D0WD-V3) Safe Profile
#define ESP32_CLASSIC_PIN_OLED_SDA    21    // Standard ESP32 Wire SDA
#define ESP32_CLASSIC_PIN_OLED_SCL    22    // Standard ESP32 Wire SCL
#define ESP32_CLASSIC_PIN_HX711_DT    18    // Safe GPIO outside SPI flash 6..11
#define ESP32_CLASSIC_PIN_HX711_SCK   19    // Safe GPIO outside SPI flash 6..11
#define ESP32_CLASSIC_PIN_STATUS_LED  4     // Safe PWM GPIO avoiding UART0 RX (3) & strap (2)
#define ESP32_CLASSIC_PIN_UART0_TX    1     // Hardware UART0 TX on ESP32
#define ESP32_CLASSIC_PIN_UART0_RX    3     // Hardware UART0 RX on ESP32

#define ESP32_CLASSIC_FLASH_PIN_MIN   6     // Integrated SPI Flash SCK/SD0..SD3/CMD start
#define ESP32_CLASSIC_FLASH_PIN_MAX   11    // Integrated SPI Flash SCK/SD0..SD3/CMD end

#if defined(BOARD_ESP32_CLASSIC) || (defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3))

#define BOARD_TARGET_NAME     "ESP32"
#define BOARD_BANNER_NAME     "ESP32 SMART CUP STAND - SYSTEM INTEGRATION"
#define BOARD_OLED_BADGE      "ESP32 Coaster"

#ifndef PIN_OLED_SDA
#define PIN_OLED_SDA          ESP32_CLASSIC_PIN_OLED_SDA
#endif

#ifndef PIN_OLED_SCL
#define PIN_OLED_SCL          ESP32_CLASSIC_PIN_OLED_SCL
#endif

#ifndef PIN_HX711_DT
#define PIN_HX711_DT          ESP32_CLASSIC_PIN_HX711_DT
#endif

#ifndef PIN_HX711_SCK
#define PIN_HX711_SCK         ESP32_CLASSIC_PIN_HX711_SCK
#endif

#ifndef PIN_STATUS_LED
#define PIN_STATUS_LED        ESP32_CLASSIC_PIN_STATUS_LED
#endif

#define PIN_UART0_TX          ESP32_CLASSIC_PIN_UART0_TX
#define PIN_UART0_RX          ESP32_CLASSIC_PIN_UART0_RX

#else // Default Profile: ESP32-C3 DevKitM-1

#define BOARD_TARGET_NAME     "ESP32-C3"
#define BOARD_BANNER_NAME     "ESP32-C3 SMART CUP STAND - SYSTEM INTEGRATION"
#define BOARD_OLED_BADGE      "ESP32-C3 Coaster"

// --- I2C OLED Display (SSD1306 128x64) ---
// Uses dedicated safe GPIOs avoiding strapping pins 8 and 9.
// Always call Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL) in firmware setup.
#ifndef PIN_OLED_SDA
#define PIN_OLED_SDA          4     // GPIO 4 (Header J3-11): I2C SDA
#endif

#ifndef PIN_OLED_SCL
#define PIN_OLED_SCL          5     // GPIO 5 (Header J3-10): I2C SCL (400kHz)
#endif

// --- HX711 24-Bit ADC / Load Cell Interface ---
#ifndef PIN_HX711_DT
#define PIN_HX711_DT          6     // GPIO 6 (Header J3-9): Data Output from HX711
#endif

#ifndef PIN_HX711_SCK
#define PIN_HX711_SCK         7     // GPIO 7 (Header J3-8): Serial Clock to HX711
#endif

// --- Status Alert LED ---
// Active-HIGH driving external LED via 220 Ohm current-limiting resistor to GND.
#ifndef PIN_STATUS_LED
#define PIN_STATUS_LED        3     // GPIO 3 (Header J1-5): Non-strapping PWM GPIO
#endif

// --- Primary Hardware UART0 Console ---
#define PIN_UART0_TX          21    // GPIO 21 (Header J3-2): Serial TX to Console
#define PIN_UART0_RX          20    // GPIO 20 (Header J3-3): Serial RX from Console

#endif // Board Profile Selection

#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR         0x3C  // Default I2C address for SSD1306 display
#endif

#define OLED_SCREEN_WIDTH     128   // OLED display width in pixels
#define OLED_SCREEN_HEIGHT    64    // OLED display height in pixels
#define OLED_RESET_PIN        -1    // Shared/no external hardware reset line

// Wokwi 5kg component: 2100 counts / 5000g = 0.42 counts/gram
#ifndef HX711_SCALE_FACTOR
#define HX711_SCALE_FACTOR    0.42f
#endif

#define HX711_DEFAULT_GAIN    128   // Channel A with 128 gain (Wokwi default)

#define LEDC_PWM_CHANNEL      0     // LEDC PWM channel
#define LEDC_PWM_FREQ_HZ      5000  // 5 kHz PWM frequency (flicker-free & silent)
#define LEDC_PWM_RESOLUTION   8     // 8-bit resolution (0..255 duty cycle)

#define SERIAL_BAUD_RATE      115200

// ============================================================================
// Strapping Pin Guard Documentation (DO NOT REASSIGN)
// ============================================================================
// ESP32-C3 Strapping Pins: GPIO 2, GPIO 8 (onboard WS2812), GPIO 9 (BOOT).
// Classic ESP32 Strapping Pins: GPIO 0 (BOOT), GPIO 2, GPIO 12 (VDD_SDIO), GPIO 15.
#define PIN_STRAP_GPIO0       0
#define PIN_STRAP_GPIO2       2
#define PIN_STRAP_GPIO8       8
#define PIN_STRAP_GPIO9       9
#define PIN_STRAP_GPIO12      12
#define PIN_STRAP_GPIO15      15

#endif // PINS_CONFIG_H_

