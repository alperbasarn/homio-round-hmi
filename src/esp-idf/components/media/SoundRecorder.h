#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Microphone.h"
#include "SDCard.h"
#include "esp_err.h"

class SoundRecorder {
public:
    SoundRecorder(SDCard* sdCard, Microphone* microphone);

    esp_err_t initialize();
    esp_err_t startRecordingToFile(const std::string& fileName,
                                   uint32_t sampleRate = 16000,
                                   uint8_t channels = 1,
                                   uint8_t bitsPerSample = 16);
    esp_err_t processRecordingChunk(uint32_t timeoutMs = 200);
    esp_err_t stopRecording();

    esp_err_t recordToFile(const std::string& fileName,
                           uint32_t durationMs = 3000,
                           uint32_t sampleRate = 16000,
                           uint8_t channels = 1,
                           uint8_t bitsPerSample = 16);

    bool isRecording() const { return recording; }
    const std::string& getCurrentRecordingPath() const { return currentRecordingPath; }
    const std::string& getLastCompletedRecordingPath() const { return lastCompletedRecordingPath; }

private:
    static constexpr size_t RECORD_CHUNK_SIZE = 2048;

    SDCard* sdCard;
    Microphone* microphone;
    bool ready;
    bool recording;
    FILE* activeFile;
    uint32_t activeSampleRate;
    uint8_t activeChannels;
    uint8_t activeBitsPerSample;
    uint32_t recordedDataBytes;
    std::string currentRecordingPath;
    std::string lastCompletedRecordingPath;
    std::vector<uint8_t> buffer;

    std::string resolveRecordingPath(const std::string& fileName) const;
    bool writeWavHeader(FILE* file,
                        uint32_t sampleRate,
                        uint8_t channels,
                        uint8_t bitsPerSample,
                        uint32_t dataBytes) const;
};
