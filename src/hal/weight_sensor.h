#ifndef HAL_WEIGHT_SENSOR_H_
#define HAL_WEIGHT_SENSOR_H_

#include <Arduino.h>
#include <HX711.h>
#include "pins_config.h"

/**
 * @brief Hardware Abstraction Layer driver for HX711 24-bit load cell ADC.
 * 
 * Provides non-blocking weight sampling, Wokwi 5kg calibration (0.42 counts/g),
 * bounded hardware taring, and disconnection detection.
 */
class WeightSensor {
public:
    WeightSensor();
    ~WeightSensor() = default;

    /**
     * @brief Initialize the HX711 sensor with specified pins and calibration factor.
     * @param pin_dt GPIO pin for HX711 DOUT (default: PIN_HX711_DT = GPIO 6)
     * @param pin_sck GPIO pin for HX711 PD_SCK (default: PIN_HX711_SCK = GPIO 7)
     * @param scale_factor Calibration factor in counts/gram (default: HX711_SCALE_FACTOR = 0.420f)
     * @param gain Channel gain (default: HX711_DEFAULT_GAIN = 128 for Channel A)
     * @return true if initialization completed
     */
    bool begin(uint8_t pin_dt = PIN_HX711_DT,
               uint8_t pin_sck = PIN_HX711_SCK,
               float scale_factor = HX711_SCALE_FACTOR,
               uint8_t gain = HX711_DEFAULT_GAIN);

    /**
     * @brief Non-blocking check whether the HX711 ADC has a new conversion ready.
     * @return true if DOUT is LOW indicating new reading is ready to retrieve.
     */
    bool isReady();

    /**
     * @brief Non-blocking calibrated weight reading in grams.
     * If a new conversion is ready, reads the sample, updates internal cache, and returns it.
     * If not ready and fallback_to_cached is true, returns the last valid filtered weight.
     * If not ready and fallback_to_cached is false, returns NAN.
     * 
     * @param fallback_to_cached Whether to return cached previous weight when sensor is busy.
     * @return Weight in grams.
     */
    float readGrams(bool fallback_to_cached = true);

    /**
     * @brief Non-blocking try-read idiom.
     * If sensor is ready, updates out_weight_g and returns true.
     * If not ready, leaves out_weight_g untouched and returns false.
     * 
     * @param out_weight_g Output reference for the read weight in grams.
     * @return true if a fresh sample was read, false otherwise.
     */
    bool tryReadGrams(float& out_weight_g);

    /**
     * @brief Hardware-level tare (zero offset calibration).
     * Bounded by timeout_ms to prevent FreeRTOS watchdog starvation.
     * 
     * @param times Number of samples to average for zero point (default: 1 for fast non-blocking)
     * @param timeout_ms Maximum time to wait for sensor readiness (default: 200ms)
     * @return true if tare succeeded, false if timed out
     */
    bool tare(uint8_t times = 1, uint32_t timeout_ms = 200);

    /**
     * @brief Set the calibration factor in counts/gram.
     * @param scale_factor Scale factor (e.g. 0.420f for Wokwi 5kg component)
     */
    void setScale(float scale_factor);

    /**
     * @brief Get the currently active calibration scale factor.
     */
    float getScale();

    /**
     * @brief Set the raw hardware tare offset.
     */
    void setOffset(long offset);

    /**
     * @brief Get the current raw hardware tare offset.
     */
    long getOffset();

    /**
     * @brief Read raw 24-bit ADC counts if ready.
     * @param fallback_to_cached Return cached raw count if busy.
     * @return Raw ADC integer count.
     */
    long readRaw(bool fallback_to_cached = true);

    /**
     * @brief Check if the sensor has produced a valid sample within timeout_ms.
     * Detects physical disconnection or hardware failure.
     */
    bool isConnected(uint32_t timeout_ms = 2000) const;

    /**
     * @brief Get the last valid weight reading in grams.
     */
    float getLastWeight() const { return last_weight_g_; }

    /**
     * @brief Check whether at least one valid reading has been obtained since boot.
     */
    bool hasValidReading() const { return has_valid_reading_; }

    /**
     * @brief Timestamp of the last successful reading in milliseconds.
     */
    uint32_t getLastReadTimeMs() const { return last_read_time_ms_; }

    /**
     * @brief Number of consecutive polling cycles where the sensor was not ready.
     */
    uint32_t getConsecutiveFailures() const { return consecutive_failures_; }

    /**
     * @brief Enter low-power sleep mode (SCK held HIGH).
     */
    void powerDown();

    /**
     * @brief Wake up from low-power sleep mode (SCK pulled LOW).
     */
    void powerUp();

private:
    HX711 scale_;
    uint8_t pin_dt_ = PIN_HX711_DT;
    uint8_t pin_sck_ = PIN_HX711_SCK;
    float scale_factor_ = HX711_SCALE_FACTOR;
    bool is_initialized_ = false;

    float last_weight_g_ = 0.0f;
    long last_raw_counts_ = 0;
    uint32_t last_read_time_ms_ = 0;
    bool has_valid_reading_ = false;
    uint32_t consecutive_failures_ = 0;
};

#endif // HAL_WEIGHT_SENSOR_H_
