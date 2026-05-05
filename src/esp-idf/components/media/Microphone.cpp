#include "Microphone.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "hal_config.h"

namespace {
constexpr const char* TAG = "Microphone";
constexpr float MIN_GAIN = 0.0f;
constexpr float MAX_GAIN = 8.0f;
}  // namespace

Microphone::Microphone()
    : rxChannel(nullptr),
      initialized(false),
      currentSampleRate(0),
      currentChannels(0),
      currentBitsPerSample(0),
      softwareGain(1.0f) {
}

Microphone::~Microphone() {
    deinitialize();
}

esp_err_t Microphone::initialize(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample) {
#if QNOB_AUDIO_INPUT_BACKEND == QNOB_AUDIO_INPUT_BACKEND_NONE
    ESP_LOGI(TAG, "Audio input is disabled for this board");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    if (initialized &&
        currentSampleRate == sampleRate &&
        currentChannels == channels &&
        currentBitsPerSample == bitsPerSample) {
        return ESP_OK;
    }

    deinitialize();
    esp_err_t err = configure(sampleRate, channels, bitsPerSample);
    if (err != ESP_OK) {
        return err;
    }

    initialized = true;
    currentSampleRate = sampleRate;
    currentChannels = channels;
    currentBitsPerSample = bitsPerSample;

    ESP_LOGI(TAG, "Microphone initialized (%lu Hz, %u ch, %u-bit)",
             static_cast<unsigned long>(sampleRate),
             static_cast<unsigned>(channels),
             static_cast<unsigned>(bitsPerSample));
    return ESP_OK;
}

void Microphone::deinitialize() {
    if (rxChannel != nullptr) {
        i2s_channel_disable(rxChannel);
        i2s_del_channel(rxChannel);
        rxChannel = nullptr;
    }
    initialized = false;
    currentSampleRate = 0;
    currentChannels = 0;
    currentBitsPerSample = 0;
}

esp_err_t Microphone::read(uint8_t* data, size_t len, size_t* bytesRead, uint32_t timeoutMs) {
    if (!initialized || rxChannel == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == nullptr || len == 0) {
        if (bytesRead != nullptr) {
            *bytesRead = 0;
        }
        return ESP_OK;
    }

    size_t readLen = 0;
    esp_err_t err = i2s_channel_read(rxChannel, data, len, &readLen, pdMS_TO_TICKS(timeoutMs));
    if (bytesRead != nullptr) {
        *bytesRead = readLen;
    }
    if (err != ESP_OK) {
        return err;
    }

    if (softwareGain > 1.001f && currentBitsPerSample == 16 && readLen >= sizeof(int16_t)) {
        const size_t sampleCount = readLen / sizeof(int16_t);
        auto* samples = reinterpret_cast<int16_t*>(data);
        for (size_t i = 0; i < sampleCount; ++i) {
            const float amplified = static_cast<float>(samples[i]) * softwareGain;
            const float clamped = std::max(-32768.0f, std::min(32767.0f, amplified));
            samples[i] = static_cast<int16_t>(std::lround(clamped));
        }
    }

    return ESP_OK;
}

void Microphone::setSoftwareGain(float gain) {
    softwareGain = std::max(MIN_GAIN, std::min(MAX_GAIN, gain));
}

esp_err_t Microphone::configure(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample) {
#if QNOB_AUDIO_INPUT_BACKEND != QNOB_AUDIO_INPUT_BACKEND_I2S
    ESP_LOGE(TAG, "Unsupported audio input backend: %d", QNOB_AUDIO_INPUT_BACKEND);
    return ESP_ERR_NOT_SUPPORTED;
#endif

    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chanCfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chanCfg, nullptr, &rxChannel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_data_bit_width_t bitWidth = I2S_DATA_BIT_WIDTH_16BIT;
    switch (bitsPerSample) {
        case 16:
            bitWidth = I2S_DATA_BIT_WIDTH_16BIT;
            break;
        case 24:
            bitWidth = I2S_DATA_BIT_WIDTH_24BIT;
            break;
        case 32:
            bitWidth = I2S_DATA_BIT_WIDTH_32BIT;
            break;
        default:
            ESP_LOGE(TAG, "Unsupported bit depth: %u", static_cast<unsigned>(bitsPerSample));
            return ESP_ERR_INVALID_ARG;
    }

    i2s_slot_mode_t slotMode = (channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_clk_config_t clkCfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
    clkCfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    clkCfg.ext_clk_freq_hz = 0;
    const gpio_num_t mclkPin = static_cast<gpio_num_t>((AUDIO_I2S_MCLK_PIN >= 0) ? AUDIO_I2S_MCLK_PIN : I2S_GPIO_UNUSED);
    const gpio_num_t bclkPin = static_cast<gpio_num_t>((AUDIO_I2S_BCLK_PIN >= 0) ? AUDIO_I2S_BCLK_PIN : I2S_GPIO_UNUSED);
    const gpio_num_t wsPin = static_cast<gpio_num_t>((AUDIO_I2S_WS_PIN >= 0) ? AUDIO_I2S_WS_PIN : I2S_GPIO_UNUSED);
    const gpio_num_t dinPin = static_cast<gpio_num_t>((AUDIO_I2S_DIN_PIN >= 0) ? AUDIO_I2S_DIN_PIN : I2S_GPIO_UNUSED);

    i2s_std_config_t stdCfg = {
        .clk_cfg = clkCfg,
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bitWidth, slotMode),
        .gpio_cfg = {
            .mclk = mclkPin,
            .bclk = bclkPin,
            .ws = wsPin,
            .dout = static_cast<gpio_num_t>(I2S_GPIO_UNUSED),
            .din = dinPin,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(rxChannel, &stdCfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
        i2s_del_channel(rxChannel);
        rxChannel = nullptr;
        return err;
    }

    err = i2s_channel_enable(rxChannel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
        i2s_del_channel(rxChannel);
        rxChannel = nullptr;
        return err;
    }

    return ESP_OK;
}
