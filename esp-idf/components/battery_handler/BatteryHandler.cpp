#include "BatteryHandler.h"
#include "hal_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_cali_scheme.h"
#include <algorithm>
#include <array>

static const char* TAG = "BatteryHandler";

BatteryHandler::BatteryHandler()
    : adcHandle(nullptr),
      adcChannel(static_cast<adc_channel_t>(BAT_ADC_PIN)),
      adcCaliHandle(nullptr),
      adcCaliScheme(AdcCalibrationScheme::NONE),
      lastMeasurementTime(0),
      batteryPercentage(0.0f),
      batteryVoltage(0.0f),
      lastRawSpread(0),
      initialized(false) {
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
}

int64_t BatteryHandler::millis() const {
    return esp_timer_get_time() / 1000;
}

esp_err_t BatteryHandler::initialize() {
    if (initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing battery ADC on channel %d", adcChannel);

    // Configure ADC unit
    adc_oneshot_unit_init_cfg_t initConfig = {};
    initConfig.unit_id = ADC_UNIT_1;
    initConfig.ulp_mode = ADC_ULP_MODE_DISABLE;

    esp_err_t err = adc_oneshot_new_unit(&initConfig, &adcHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(err));
        return err;
    }

    // Configure ADC channel
    adc_oneshot_chan_cfg_t chanConfig = {};
    chanConfig.atten = ADC_ATTEN_DB_12;  // Full range: 0-3.3V
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

    initialized = true;
    ESP_LOGI(TAG, "Battery handler initialized successfully");

    // Take initial VBAT reading. Presence cannot be inferred from ADC reliably.
    const float initialVoltage = readVoltage();
    batteryVoltage = initialVoltage;
    batteryPercentage = voltageToPercentage(initialVoltage);
    lastMeasurementTime = millis();
    ESP_LOGI(TAG, "Initial VBAT estimate: %.2fV (%.0f%%, spread=%d)",
             batteryVoltage, batteryPercentage, lastRawSpread);

    return ESP_OK;
}

float BatteryHandler::readVoltage() {
    if (!initialized || adcHandle == nullptr) {
        return 0.0f;
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
        return batteryVoltage;  // Return last known value
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
    if (adcCaliHandle != nullptr) {
        if (adc_cali_raw_to_voltage(adcCaliHandle, filteredRaw, &adcMilliVolts) == ESP_OK) {
            return (static_cast<float>(adcMilliVolts) / 1000.0f) * VOLTAGE_DIVIDER_RATIO;
        }
    }

    // Fallback raw conversion if calibration is not available.
    const float adcVoltage = (static_cast<float>(filteredRaw) / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    return adcVoltage * VOLTAGE_DIVIDER_RATIO;
}

float BatteryHandler::voltageToPercentage(float voltage) {
    struct VoltagePoint {
        float voltage;
        float percentage;
    };

    // Li-ion SOC lookup (idle voltage approximation).
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

void BatteryHandler::update() {
    if (!initialized) {
        return;
    }

    int64_t currentMillis = millis();
    if (currentMillis - lastMeasurementTime < MEASUREMENT_INTERVAL_MS) {
        return;
    }

    const float measuredVoltage = readVoltage();

    // Smooth periodic VBAT estimate for a stable UI readout.
    if (batteryVoltage <= 0.0f || measuredVoltage <= 0.0f) {
        batteryVoltage = measuredVoltage;
    } else {
        batteryVoltage = (batteryVoltage * 0.8f) + (measuredVoltage * 0.2f);
    }
    batteryPercentage = voltageToPercentage(batteryVoltage);
    lastMeasurementTime = currentMillis;

    ESP_LOGD(TAG, "VBAT estimate: %.2fV (%.0f%%, rawSpread=%d)", batteryVoltage, batteryPercentage, lastRawSpread);
}

bool BatteryHandler::isCharging() const {
    // This would require a separate GPIO to detect charging state
    // For now, return false. Can be enhanced later if charging pin is available.
    return false;
}
