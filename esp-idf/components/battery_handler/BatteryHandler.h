#pragma once

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include <cstdint>

class BatteryHandler {
private:
    enum class AdcCalibrationScheme : uint8_t {
        NONE = 0,
        CURVE_FITTING,
        LINE_FITTING,
    };

    adc_oneshot_unit_handle_t adcHandle;
    adc_channel_t adcChannel;
    adc_cali_handle_t adcCaliHandle;
    AdcCalibrationScheme adcCaliScheme;
    int64_t lastMeasurementTime;
    float batteryPercentage;
    float batteryVoltage;
    int lastRawSpread;
    bool initialized;

    static constexpr int64_t MEASUREMENT_INTERVAL_MS = 30000;
    static constexpr float VOLTAGE_DIVIDER_RATIO = 2.0f;  // Typical 50% voltage divider
    static constexpr float ADC_REF_VOLTAGE = 3.3f;
    static constexpr int ADC_MAX_VALUE = 4095;  // 12-bit ADC

    float readVoltage();
    float voltageToPercentage(float voltage);
    int64_t millis() const;

public:
    BatteryHandler();
    ~BatteryHandler();

    esp_err_t initialize();
    void update();

    float getBatteryPercentage() const { return batteryPercentage; }
    float getBatteryVoltage() const { return batteryVoltage; }
    bool isBatteryConnected() const { return false; }  // Presence is unknown with ADC-only measurement.
    bool isInitialized() const { return initialized; }
    bool isCharging() const;
};
