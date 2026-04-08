#include "BatteryHandler.h"

#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal_config.h"
#include "hal_init.h"

#include <algorithm>
#include <array>

static const char* TAG = "BatteryHandler";

namespace {

constexpr uint8_t AXP2101_REG_STATUS1 = 0x00;
constexpr uint8_t AXP2101_REG_STATUS2 = 0x01;
constexpr uint8_t AXP2101_REG_IC_TYPE = 0x03;
constexpr uint8_t AXP2101_REG_ADC_CHANNEL_CTRL = 0x30;
constexpr uint8_t AXP2101_REG_ADC_DATA_RESULT0 = 0x34;
constexpr uint8_t AXP2101_REG_ADC_DATA_RESULT1 = 0x35;
constexpr uint8_t AXP2101_REG_BAT_DET_CTRL = 0x68;
constexpr uint8_t AXP2101_REG_BAT_PERCENT_DATA = 0xA4;

constexpr uint8_t AXP2101_CHIP_ID = 0x4A;
constexpr uint8_t AXP2101_STATUS1_BAT_PRESENT_BIT = 3;
constexpr uint8_t AXP2101_ADC_BAT_VOLTAGE_ENABLE_BIT = 0;
constexpr uint8_t AXP2101_ADC_TS_MEASURE_ENABLE_BIT = 1;
constexpr uint8_t AXP2101_BAT_DETECT_ENABLE_BIT = 0;
constexpr uint8_t AXP2101_CHARGER_STATE_MASK = 0x07;

bool axp2101ChargeStateMeansCharging(uint8_t state)
{
    // Mirrors the XPowers enum ordering used by Waveshare's official example:
    // tri-charge, pre-charge, constant-current, constant-voltage, done, stop.
    return state <= 3;
}

}  // namespace

BatteryHandler::BatteryHandler()
    : adcHandle(nullptr),
      adcChannel(static_cast<adc_channel_t>(0)),
      adcCaliHandle(nullptr),
      pmuHandle(nullptr),
      adcCaliScheme(AdcCalibrationScheme::NONE),
      config{},
      batteryTelemetry{},
      lastMeasurementTime(0),
      lastRawSpread(0),
      charging(false),
      initialized(false) {
    config.measurementIntervalMs = DEFAULT_MEASUREMENT_INTERVAL_MS;
    config.voltageDividerRatio = DEFAULT_VOLTAGE_DIVIDER_RATIO;
    resetTelemetry();
}

BatteryHandler::~BatteryHandler() {
    if (adcCaliHandle != nullptr) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        if (adcCaliScheme == AdcCalibrationScheme::CURVE_FITTING) {
            adc_cali_delete_scheme_curve_fitting(adcCaliHandle);
        }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        if (adcCaliScheme == AdcCalibrationScheme::LINE_FITTING) {
            adc_cali_delete_scheme_line_fitting(adcCaliHandle);
        }
#endif
        adcCaliHandle = nullptr;
        adcCaliScheme = AdcCalibrationScheme::NONE;
    }

    if (adcHandle != nullptr) {
        adc_oneshot_del_unit(adcHandle);
        adcHandle = nullptr;
    }

    if (pmuHandle != nullptr) {
        i2c_master_bus_rm_device(pmuHandle);
        pmuHandle = nullptr;
    }
}

void BatteryHandler::resetTelemetry() {
    batteryTelemetry = {};
    charging = false;
}

int64_t BatteryHandler::millis() const {
    return esp_timer_get_time() / 1000;
}

esp_err_t BatteryHandler::initialize(const Config& initConfig) {
    if (initialized) {
        return ESP_OK;
    }

    config = initConfig;
    if (config.measurementIntervalMs <= 0) {
        config.measurementIntervalMs = DEFAULT_MEASUREMENT_INTERVAL_MS;
    }
    if (config.voltageDividerRatio <= 0.0f) {
        config.voltageDividerRatio = DEFAULT_VOLTAGE_DIVIDER_RATIO;
    }

    resetTelemetry();
    lastMeasurementTime = 0;
    lastRawSpread = 0;

    esp_err_t err = ESP_OK;
    switch (config.source) {
        case TelemetrySource::NONE:
            ESP_LOGI(TAG, "Battery telemetry source disabled for this board");
            break;

        case TelemetrySource::ADC:
            err = initializeAdcBackend();
            break;

        case TelemetrySource::AXP2101:
            err = initializePmuBackend();
            break;

        default:
            ESP_LOGE(TAG, "Unsupported battery telemetry source: %u",
                     static_cast<unsigned>(config.source));
            return ESP_ERR_INVALID_ARG;
    }

    if (err != ESP_OK) {
        return err;
    }

    initialized = true;
    ESP_LOGI(TAG, "Battery handler initialized (source=%u)", static_cast<unsigned>(config.source));

    // Populate the first snapshot through the same backend-specific switch used for runtime updates.
    analyze();
    return ESP_OK;
}

esp_err_t BatteryHandler::initializeAdcBackend() {
    if (config.adcChannel < 0) {
        ESP_LOGE(TAG, "ADC telemetry source selected without a valid ADC channel");
        return ESP_ERR_INVALID_ARG;
    }

    adcChannel = static_cast<adc_channel_t>(config.adcChannel);
    ESP_LOGI(TAG, "Initializing battery ADC on channel %d", config.adcChannel);

    adc_oneshot_unit_init_cfg_t initConfig = {};
    initConfig.unit_id = ADC_UNIT_1;
    initConfig.ulp_mode = ADC_ULP_MODE_DISABLE;

    esp_err_t err = adc_oneshot_new_unit(&initConfig, &adcHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chanConfig = {};
    chanConfig.atten = ADC_ATTEN_DB_12;
    chanConfig.bitwidth = ADC_BITWIDTH_12;

    err = adc_oneshot_config_channel(adcHandle, adcChannel, &chanConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(adcHandle);
        adcHandle = nullptr;
        return err;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t curveConfig = {};
    curveConfig.unit_id = ADC_UNIT_1;
    curveConfig.atten = ADC_ATTEN_DB_12;
    curveConfig.bitwidth = ADC_BITWIDTH_12;

    if (adc_cali_create_scheme_curve_fitting(&curveConfig, &adcCaliHandle) == ESP_OK) {
        adcCaliScheme = AdcCalibrationScheme::CURVE_FITTING;
        ESP_LOGI(TAG, "ADC calibration enabled (curve fitting)");
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (adcCaliHandle == nullptr) {
        adc_cali_line_fitting_config_t lineConfig = {};
        lineConfig.unit_id = ADC_UNIT_1;
        lineConfig.atten = ADC_ATTEN_DB_12;
        lineConfig.bitwidth = ADC_BITWIDTH_12;
        lineConfig.default_vref = 0;
        if (adc_cali_create_scheme_line_fitting(&lineConfig, &adcCaliHandle) == ESP_OK) {
            adcCaliScheme = AdcCalibrationScheme::LINE_FITTING;
            ESP_LOGI(TAG, "ADC calibration enabled (line fitting)");
        }
    }
#endif

    if (adcCaliHandle == nullptr) {
        ESP_LOGW(TAG, "ADC calibration not available; using raw conversion");
    }

    return ESP_OK;
}

esp_err_t BatteryHandler::pmuReadRegister(uint8_t reg, uint8_t* data, size_t len) const {
    if (pmuHandle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(pmuHandle, &reg, 1, data, len, I2C_MASTER_TIMEOUT_MS);
}

esp_err_t BatteryHandler::pmuWriteRegister(uint8_t reg, uint8_t value) const {
    if (pmuHandle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t tx[2] = {reg, value};
    return i2c_master_transmit(pmuHandle, tx, sizeof(tx), I2C_MASTER_TIMEOUT_MS);
}

esp_err_t BatteryHandler::pmuUpdateRegisterBits(uint8_t reg, uint8_t mask, uint8_t value) const {
    uint8_t currentValue = 0;
    esp_err_t err = pmuReadRegister(reg, &currentValue, 1);
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t updatedValue = static_cast<uint8_t>((currentValue & ~mask) | (value & mask));
    if (updatedValue == currentValue) {
        return ESP_OK;
    }

    return pmuWriteRegister(reg, updatedValue);
}

bool BatteryHandler::pmuReadRegisterU8(uint8_t reg, uint8_t& value) const {
    return pmuReadRegister(reg, &value, 1) == ESP_OK;
}

bool BatteryHandler::pmuReadRegisterH5L8(uint8_t highReg, uint8_t lowReg, uint16_t& value) const {
    uint8_t packed[2] = {0};
    if (lowReg != highReg + 1) {
        uint8_t highByte = 0;
        uint8_t lowByte = 0;
        if (!pmuReadRegisterU8(highReg, highByte) || !pmuReadRegisterU8(lowReg, lowByte)) {
            return false;
        }
        packed[0] = highByte;
        packed[1] = lowByte;
    } else if (pmuReadRegister(highReg, packed, sizeof(packed)) != ESP_OK) {
        return false;
    }

    value = static_cast<uint16_t>(((packed[0] & 0x1F) << 8) | packed[1]);
    return true;
}

esp_err_t BatteryHandler::initializePmuBackend() {
    if (config.pmuI2cAddress < 0) {
        ESP_LOGE(TAG, "AXP2101 telemetry source selected without a valid PMU I2C address");
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_handle_t busHandle = hal_i2c_get_bus_handle();
    if (busHandle == nullptr) {
        ESP_LOGE(TAG, "I2C bus is not initialized for PMU telemetry");
        return ESP_ERR_INVALID_STATE;
    }

    if (pmuHandle == nullptr) {
        i2c_device_config_t devCfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = static_cast<uint16_t>(config.pmuI2cAddress),
            .scl_speed_hz = 100000,
            .scl_wait_us = 0,
            .flags = {
                .disable_ack_check = false,
            },
        };
        esp_err_t addErr = i2c_master_bus_add_device(busHandle, &devCfg, &pmuHandle);
        if (addErr != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add AXP2101 at 0x%02X: %s",
                     config.pmuI2cAddress, esp_err_to_name(addErr));
            return addErr;
        }
    }

    uint8_t chipId = 0;
    if (!pmuReadRegisterU8(AXP2101_REG_IC_TYPE, chipId)) {
        ESP_LOGE(TAG, "Failed to read AXP2101 chip ID");
        return ESP_FAIL;
    }
    if (chipId != AXP2101_CHIP_ID) {
        ESP_LOGW(TAG, "Unexpected AXP2101 chip ID 0x%02X at 0x%02X",
                 chipId, config.pmuI2cAddress);
    }

    esp_err_t err = pmuUpdateRegisterBits(
        AXP2101_REG_ADC_CHANNEL_CTRL,
        static_cast<uint8_t>((1U << AXP2101_ADC_BAT_VOLTAGE_ENABLE_BIT) |
                             (1U << AXP2101_ADC_TS_MEASURE_ENABLE_BIT)),
        static_cast<uint8_t>(1U << AXP2101_ADC_BAT_VOLTAGE_ENABLE_BIT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure AXP2101 ADC channels: %s", esp_err_to_name(err));
        return err;
    }

    err = pmuUpdateRegisterBits(
        AXP2101_REG_BAT_DET_CTRL,
        static_cast<uint8_t>(1U << AXP2101_BAT_DETECT_ENABLE_BIT),
        static_cast<uint8_t>(1U << AXP2101_BAT_DETECT_ENABLE_BIT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable AXP2101 battery detection: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "AXP2101 PMU telemetry initialized at 0x%02X (chip=0x%02X)",
             config.pmuI2cAddress, chipId);
    return ESP_OK;
}

float BatteryHandler::readVoltage() {
    if (!initialized || adcHandle == nullptr) {
        return -1.0f;
    }

    constexpr int SAMPLE_COUNT = 12;
    std::array<int, SAMPLE_COUNT> rawSamples = {};
    int validSamples = 0;

    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        int rawValue = 0;
        esp_err_t err = adc_oneshot_read(adcHandle, adcChannel, &rawValue);
        if (err == ESP_OK) {
            rawSamples[validSamples++] = rawValue;
        }
    }

    if (validSamples == 0) {
        ESP_LOGW(TAG, "ADC read failed for all samples");
        return batteryTelemetry.voltageVolts;
    }

    std::sort(rawSamples.begin(), rawSamples.begin() + validSamples);
    lastRawSpread = rawSamples[validSamples - 1] - rawSamples[0];

    const int trim = (validSamples >= 6) ? (validSamples / 4) : 0;
    const int begin = trim;
    const int end = validSamples - trim;
    int64_t sum = 0;
    for (int i = begin; i < end; ++i) {
        sum += rawSamples[i];
    }
    const int usedSamples = std::max(1, end - begin);
    const int filteredRaw = static_cast<int>(sum / usedSamples);

    int adcMilliVolts = 0;
    if (adcCaliHandle != nullptr &&
        adc_cali_raw_to_voltage(adcCaliHandle, filteredRaw, &adcMilliVolts) == ESP_OK) {
        return (static_cast<float>(adcMilliVolts) / 1000.0f) * config.voltageDividerRatio;
    }

    const float adcVoltage = (static_cast<float>(filteredRaw) / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    return adcVoltage * config.voltageDividerRatio;
}

float BatteryHandler::voltageToPercentage(float voltage) {
    struct VoltagePoint {
        float voltage;
        float percentage;
    };

    static constexpr std::array<VoltagePoint, 13> SOC_CURVE = {{
        {4.20f, 100.0f},
        {4.11f, 90.0f},
        {4.03f, 80.0f},
        {3.95f, 70.0f},
        {3.88f, 60.0f},
        {3.82f, 50.0f},
        {3.77f, 40.0f},
        {3.72f, 30.0f},
        {3.67f, 20.0f},
        {3.61f, 12.0f},
        {3.54f, 7.0f},
        {3.45f, 3.0f},
        {3.30f, 0.0f},
    }};

    if (voltage >= SOC_CURVE.front().voltage) {
        return 100.0f;
    }
    if (voltage <= SOC_CURVE.back().voltage) {
        return 0.0f;
    }

    for (size_t i = 0; i + 1 < SOC_CURVE.size(); ++i) {
        const auto& high = SOC_CURVE[i];
        const auto& low = SOC_CURVE[i + 1];
        if (voltage <= high.voltage && voltage >= low.voltage) {
            const float spanV = high.voltage - low.voltage;
            if (spanV <= 0.0001f) {
                return low.percentage;
            }
            const float t = (voltage - low.voltage) / spanV;
            return low.percentage + t * (high.percentage - low.percentage);
        }
    }

    return 0.0f;
}

void BatteryHandler::analyzeAdcTelemetry(int64_t currentMillis) {
    if (lastMeasurementTime != 0 &&
        currentMillis - lastMeasurementTime < config.measurementIntervalMs) {
        return;
    }

    const float measuredVoltage = readVoltage();
    batteryTelemetry.currentMilliamps = -1.0f;

    if (measuredVoltage >= 0.0f) {
        if (batteryTelemetry.voltageVolts < 0.0f) {
            batteryTelemetry.voltageVolts = measuredVoltage;
        } else {
            batteryTelemetry.voltageVolts =
                (batteryTelemetry.voltageVolts * 0.8f) + (measuredVoltage * 0.2f);
        }
    }

    batteryTelemetry.percentage =
        (batteryTelemetry.voltageVolts >= 0.0f)
            ? voltageToPercentage(batteryTelemetry.voltageVolts)
            : -1.0f;
    lastMeasurementTime = currentMillis;

    ESP_LOGD(TAG, "ADC VBAT estimate: %.2fV (%.0f%%, rawSpread=%d)",
             batteryTelemetry.voltageVolts, batteryTelemetry.percentage, lastRawSpread);
}

void BatteryHandler::analyzePmuTelemetry(int64_t currentMillis) {
    if (lastMeasurementTime != 0 &&
        currentMillis - lastMeasurementTime < config.measurementIntervalMs) {
        return;
    }

    uint8_t status1 = 0;
    if (!pmuReadRegisterU8(AXP2101_REG_STATUS1, status1)) {
        ESP_LOGW(TAG, "Failed to read AXP2101 status register 1");
        charging = false;
        batteryTelemetry.currentMilliamps = -1.0f;
        lastMeasurementTime = currentMillis;
        return;
    }

    const bool batteryPresent = (status1 & (1U << AXP2101_STATUS1_BAT_PRESENT_BIT)) != 0;
    if (!batteryPresent) {
        resetTelemetry();
        lastMeasurementTime = currentMillis;
        ESP_LOGD(TAG, "AXP2101 reports no battery connected");
        return;
    }

    uint8_t status2 = 0;
    if (pmuReadRegisterU8(AXP2101_REG_STATUS2, status2)) {
        charging = axp2101ChargeStateMeansCharging(status2 & AXP2101_CHARGER_STATE_MASK);
    } else {
        ESP_LOGW(TAG, "Failed to read AXP2101 status register 2");
        charging = false;
    }

    batteryTelemetry.currentMilliamps = -1.0f;

    uint16_t batteryMilliVolts = 0;
    if (pmuReadRegisterH5L8(AXP2101_REG_ADC_DATA_RESULT0, AXP2101_REG_ADC_DATA_RESULT1,
                            batteryMilliVolts) &&
        batteryMilliVolts > 0) {
        const float measuredVoltage = static_cast<float>(batteryMilliVolts) / 1000.0f;
        if (batteryTelemetry.voltageVolts < 0.0f) {
            batteryTelemetry.voltageVolts = measuredVoltage;
        } else {
            batteryTelemetry.voltageVolts =
                (batteryTelemetry.voltageVolts * 0.8f) + (measuredVoltage * 0.2f);
        }
    } else if (batteryTelemetry.voltageVolts < 0.0f) {
        ESP_LOGW(TAG, "Failed to read AXP2101 battery voltage");
    }

    uint8_t batteryPercent = 0;
    if (pmuReadRegisterU8(AXP2101_REG_BAT_PERCENT_DATA, batteryPercent) && batteryPercent <= 100) {
        batteryTelemetry.percentage = static_cast<float>(batteryPercent);
    } else {
        batteryTelemetry.percentage =
            (batteryTelemetry.voltageVolts >= 0.0f)
                ? voltageToPercentage(batteryTelemetry.voltageVolts)
                : -1.0f;
    }

    lastMeasurementTime = currentMillis;

    ESP_LOGD(TAG, "PMU VBAT estimate: %.2fV (%.0f%%, charging=%s)",
             batteryTelemetry.voltageVolts,
             batteryTelemetry.percentage,
             charging ? "true" : "false");
}

void BatteryHandler::analyze() {
    if (!initialized) {
        return;
    }

    const int64_t currentMillis = millis();
    switch (config.source) {
        case TelemetrySource::NONE:
            resetTelemetry();
            break;

        case TelemetrySource::ADC:
            analyzeAdcTelemetry(currentMillis);
            break;

        case TelemetrySource::AXP2101:
            analyzePmuTelemetry(currentMillis);
            break;
    }
}

bool BatteryHandler::isBatteryConnected() const {
    return batteryTelemetry.percentage >= 0.0f ||
           batteryTelemetry.voltageVolts >= 0.0f ||
           batteryTelemetry.currentMilliamps >= 0.0f;
}

bool BatteryHandler::isCharging() const {
    return charging;
}
