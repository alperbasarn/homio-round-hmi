#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2s_std.h"
#include "esp_err.h"

class Speaker {
public:
    Speaker();
    ~Speaker();

    esp_err_t initialize(uint32_t sampleRate = 24000, uint8_t channels = 2, uint8_t bitsPerSample = 16);
    void deinitialize();

    bool isInitialized() const { return initialized; }
    esp_err_t write(const uint8_t* data, size_t len, size_t* written = nullptr, uint32_t timeoutMs = 1000);

    void setSoftwareVolume(float volume);
    float getSoftwareVolume() const { return softwareVolume; }

private:
    i2s_chan_handle_t txChannel;
    bool initialized;
    uint32_t currentSampleRate;
    uint8_t currentChannels;
    uint8_t currentBitsPerSample;
    float softwareVolume;

    esp_err_t enableAmplifier();
    esp_err_t configure(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample);
};
