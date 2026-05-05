#pragma once

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "driver/i2c_master.h"
#include <cstddef>
#include <cstdint>

class BatteryHandler {
public:
    enum class TelemetrySource : uint8_t {
        NONE = 0,
        ADC,
        AXP2101,
    };

    struct Config {
        TelemetrySource source = TelemetrySource::NONE;
        int adcChannel = -1;
        int pmuI2cAddress = -1;
        int64_t measurementIntervalMs = 30000;
        float voltageDividerRatio = 2.0f;
    };

    struct BatteryTelemetry {
        float percentage = -1.0f;
        float voltageVolts = -1.0f;
        float currentMilliamps = -1.0f;
    };

private:
    enum class AdcCalibrationScheme : uint8_t {
        NONE = 0,
        CURVE_FITTING,
        LINE_FITTING,
    };

    adc_oneshot_unit_handle_t adcHandle;
    adc_channel_t adcChannel;
    adc_cali_handle_t adcCaliHandle;
    i2c_master_dev_handle_t pmuHandle;
    AdcCalibrationScheme adcCaliScheme;
    Config config;
    BatteryTelemetry batteryTelemetry;
    int64_t lastMeasurementTime;
    int lastRawSpread;
    bool charging;
    bool initialized;

    static constexpr int64_t DEFAULT_MEASUREMENT_INTERVAL_MS = 30000;
    static constexpr float DEFAULT_VOLTAGE_DIVIDER_RATIO = 2.0f;
    static constexpr float ADC_REF_VOLTAGE = 3.3f;
    static constexpr int ADC_MAX_VALUE = 4095;

    void resetTelemetry();
    float readVoltage();
    float voltageToPercentage(float voltage);
    int64_t millis() const;
    esp_err_t initializeAdcBackend();
    esp_err_t initializePmuBackend();
    esp_err_t pmuReadRegister(uint8_t reg, uint8_t* data, size_t len) const;
    esp_err_t pmuWriteRegister(uint8_t reg, uint8_t value) const;
    esp_err_t pmuUpdateRegisterBits(uint8_t reg, uint8_t mask, uint8_t value) const;
    bool pmuReadRegisterU8(uint8_t reg, uint8_t& value) const;
    bool pmuReadRegisterH5L8(uint8_t highReg, uint8_t lowReg, uint16_t& value) const;
    void analyzeAdcTelemetry(int64_t currentMillis);
    void analyzePmuTelemetry(int64_t currentMillis);

public:
    BatteryHandler();
    ~BatteryHandler();

    esp_err_t initialize(const Config& initConfig);
    void analyze();

    BatteryTelemetry getBatteryTelemetry() const { return batteryTelemetry; }
    bool isBatteryConnected() const;
    bool isInitialized() const { return initialized; }
    bool isCharging() const;
};
