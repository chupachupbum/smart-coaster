#include "weight_sensor.h"
#include <cmath>

WeightSensor::WeightSensor()
    : pin_dt_(PIN_HX711_DT)
    , pin_sck_(PIN_HX711_SCK)
    , scale_factor_(HX711_SCALE_FACTOR)
    , is_initialized_(false)
    , last_weight_g_(0.0f)
    , last_raw_counts_(0)
    , last_read_time_ms_(0)
    , has_valid_reading_(false)
    , consecutive_failures_(0)
{
}

bool WeightSensor::begin(uint8_t pin_dt, uint8_t pin_sck, float scale_factor, uint8_t gain) {
    pin_dt_ = pin_dt;
    pin_sck_ = pin_sck;
    scale_factor_ = scale_factor;

    // Initialize underlying bogde/HX711 driver
    scale_.begin(pin_dt_, pin_sck_, gain);

    // Apply calibration scale factor (0.420 counts/gram for Wokwi 5kg component)
    scale_.set_scale(scale_factor_);

    // Set default initial zero offset to 0 (CoasterDomain manages dynamic auto-tare)
    scale_.set_offset(0);

    is_initialized_ = true;
    last_weight_g_ = 0.0f;
    last_raw_counts_ = 0;
    last_read_time_ms_ = millis();
    has_valid_reading_ = false;
    consecutive_failures_ = 0;

    return true;
}

bool WeightSensor::isReady() {
    if (!is_initialized_) {
        return false;
    }
    return scale_.is_ready();
}

float WeightSensor::readGrams(bool fallback_to_cached) {
    if (!is_initialized_) {
        return fallback_to_cached ? last_weight_g_ : NAN;
    }

    if (scale_.is_ready()) {
        // Safe to read: HX711 DOUT is LOW, reading will execute immediately (~100us)
        // without blocking in wait_ready().
        float val = scale_.get_units(1);
        last_weight_g_ = val;
        last_read_time_ms_ = millis();
        has_valid_reading_ = true;
        consecutive_failures_ = 0;
        return val;
    }

    // Sensor busy performing ADC conversion
    consecutive_failures_++;
    return fallback_to_cached ? last_weight_g_ : NAN;
}

bool WeightSensor::tryReadGrams(float& out_weight_g) {
    if (!is_initialized_ || !scale_.is_ready()) {
        return false;
    }

    float val = scale_.get_units(1);
    last_weight_g_ = val;
    last_read_time_ms_ = millis();
    has_valid_reading_ = true;
    consecutive_failures_ = 0;
    out_weight_g = val;
    return true;
}

bool WeightSensor::tare(uint8_t times, uint32_t timeout_ms) {
    if (!is_initialized_) {
        return false;
    }

    // Bounded readiness check prevents FreeRTOS task starvation
    if (!scale_.wait_ready_timeout(timeout_ms, 1)) {
        return false;
    }

    scale_.tare(times);
    last_weight_g_ = 0.0f;
    last_read_time_ms_ = millis();
    has_valid_reading_ = true;
    return true;
}

void WeightSensor::setScale(float scale_factor) {
    scale_factor_ = scale_factor;
    scale_.set_scale(scale_factor_);
}

float WeightSensor::getScale() {
    return scale_.get_scale();
}

void WeightSensor::setOffset(long offset) {
    scale_.set_offset(offset);
}

long WeightSensor::getOffset() {
    return scale_.get_offset();
}

long WeightSensor::readRaw(bool fallback_to_cached) {
    if (!is_initialized_) {
        return fallback_to_cached ? last_raw_counts_ : 0;
    }

    if (scale_.is_ready()) {
        long raw = scale_.read();
        last_raw_counts_ = raw;
        last_read_time_ms_ = millis();
        has_valid_reading_ = true;
        consecutive_failures_ = 0;
        return raw;
    }

    return fallback_to_cached ? last_raw_counts_ : 0;
}

bool WeightSensor::isConnected(uint32_t timeout_ms) const {
    if (!is_initialized_) {
        return false;
    }
    return (millis() - last_read_time_ms_) < timeout_ms;
}

void WeightSensor::powerDown() {
    if (is_initialized_) {
        scale_.power_down();
    }
}

void WeightSensor::powerUp() {
    if (is_initialized_) {
        scale_.power_up();
    }
}
