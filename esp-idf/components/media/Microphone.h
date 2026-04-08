#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2s_std.h"
#include "esp_err.h"

class Microphone {
public:
    Microphone();
    ~Microphone();

    esp_err_t initialize(uint32_t sampleRate = 16000, uint8_t channels = 1, uint8_t bitsPerSample = 16);
    void deinitialize();

    bool isInitialized() const { return initialized; }
    esp_err_t read(uint8_t* data, size_t len, size_t* bytesRead = nullptr, uint32_t timeoutMs = 1000);

    void setSoftwareGain(float gain);
    float getSoftwareGain() const { return softwareGain; }

private:
    i2s_chan_handle_t rxChannel;
    bool initialized;
    uint32_t currentSampleRate;
    uint8_t currentChannels;
    uint8_t currentBitsPerSample;
    float softwareGain;

    esp_err_t configure(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample);
};
