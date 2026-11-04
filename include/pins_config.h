#ifndef PINS_CONFIG_H_
#define PINS_CONFIG_H_

#include <stdint.h>

// ============================================================================
// ESP32-C3 DevKitM-1 Hardware Pinout & Peripheral Configuration
// ============================================================================

// --- I2C OLED Display (SSD1306 128x64) ---
// Uses dedicated safe GPIOs avoiding strapping pins 8 and 9.
// Always call Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL) in firmware setup.
#ifndef PIN_OLED_SDA
#define PIN_OLED_SDA          4     // GPIO 4 (Header J3-11): I2C SDA
#endif

#ifndef PIN_OLED_SCL
#define PIN_OLED_SCL          5     // GPIO 5 (Header J3-10): I2C SCL (400kHz)
#endif

#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR         0x3C  // Default I2C address for SSD1306 display
#endif

#define OLED_SCREEN_WIDTH     128   // OLED display width in pixels
#define OLED_SCREEN_HEIGHT    64    // OLED display height in pixels
#define OLED_RESET_PIN        -1    // Shared/no external hardware reset line

// --- HX711 24-Bit ADC / Load Cell Interface ---
#ifndef PIN_HX711_DT
#define PIN_HX711_DT          6     // GPIO 6 (Header J3-9): Data Output from HX711
#endif

#ifndef PIN_HX711_SCK
#define PIN_HX711_SCK         7     // GPIO 7 (Header J3-8): Serial Clock to HX711
#endif

// Wokwi 5kg component: 2100 counts / 5000g = 0.42 counts/gram
#ifndef HX711_SCALE_FACTOR
#define HX711_SCALE_FACTOR    0.42f
#endif

#define HX711_DEFAULT_GAIN    128   // Channel A with 128 gain (Wokwi default)

// --- Status Alert LED ---
// Active-HIGH driving external LED via 220 Ohm current-limiting resistor to GND.
#ifndef PIN_STATUS_LED
#define PIN_STATUS_LED        3     // GPIO 3 (Header J1-5): Non-strapping PWM GPIO
#endif

#define LEDC_PWM_CHANNEL      0     // ESP32-C3 LEDC channel
#define LEDC_PWM_FREQ_HZ      5000  // 5 kHz PWM frequency (flicker-free & silent)
#define LEDC_PWM_RESOLUTION   8     // 8-bit resolution (0..255 duty cycle)

// --- Primary Hardware UART0 Console ---
#define PIN_UART0_TX          21    // GPIO 21 (Header J3-2): Serial TX to Console
#define PIN_UART0_RX          20    // GPIO 20 (Header J3-3): Serial RX from Console
#define SERIAL_BAUD_RATE      115200

// ============================================================================
// ESP32-C3 Strapping Pin Guard Documentation (DO NOT REASSIGN)
// ============================================================================
// GPIO 2: Strapping pin (must not be pulled low during download boot).
// GPIO 8: Strapping pin & hardwired onboard WS2812 RGB LED (Header J3-6).
// GPIO 9: Primary Strapping pin & onboard BOOT button (Active-LOW).
//
// CRITICAL WARNING: Default Arduino Wire.begin() defaults to SDA=8, SCL=9.
// If GPIO 9 is pulled low at boot, MCU enters ROM download bootloader mode.
// Never attach active loads, LEDs, or pull-downs to GPIO 2, 8, or 9.
#define PIN_STRAP_GPIO2       2
#define PIN_STRAP_GPIO8       8
#define PIN_STRAP_GPIO9       9

#endif // PINS_CONFIG_H_

