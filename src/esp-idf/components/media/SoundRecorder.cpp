#include "SoundRecorder.h"

#include <array>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr const char* TAG = "SoundRecorder";

inline void writeLe16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

inline void writeLe32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}
}  // namespace

SoundRecorder::SoundRecorder(SDCard* sdCardValue, Microphone* microphoneValue)
    : sdCard(sdCardValue),
      microphone(microphoneValue),
      ready(false),
      recording(false),
      activeFile(nullptr),
      activeSampleRate(0),
      activeChannels(0),
      activeBitsPerSample(0),
      recordedDataBytes(0),
      currentRecordingPath(""),
      lastCompletedRecordingPath(""),
      buffer(RECORD_CHUNK_SIZE) {
}

esp_err_t SoundRecorder::initialize() {
    if (sdCard == nullptr || microphone == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!sdCard->isMounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    ready = true;
    return ESP_OK;
}

esp_err_t SoundRecorder::startRecordingToFile(const std::string& fileName,
                                              uint32_t sampleRate,
                                              uint8_t channels,
                                              uint8_t bitsPerSample) {
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (fileName.empty()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (recording) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::string path = resolveRecordingPath(fileName);
    FILE* file = fopen(path.c_str(), "wb+");
    if (file == nullptr) {
        ESP_LOGE(TAG, "Failed to open recording file: %s", path.c_str());
        return ESP_FAIL;
    }

    if (!writeWavHeader(file, sampleRate, channels, bitsPerSample, 0)) {
        fclose(file);
        return ESP_FAIL;
    }

    esp_err_t err = microphone->initialize(sampleRate, channels, bitsPerSample);
    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    activeFile = file;
    activeSampleRate = sampleRate;
    activeChannels = channels;
    activeBitsPerSample = bitsPerSample;
    recordedDataBytes = 0;
    currentRecordingPath = path;
    recording = true;

    ESP_LOGI(TAG, "Recording started: %s", path.c_str());
    return ESP_OK;
}

esp_err_t SoundRecorder::processRecordingChunk(uint32_t timeoutMs) {
    if (!recording || activeFile == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytesRead = 0;
    esp_err_t err = microphone->read(buffer.data(), buffer.size(), &bytesRead, timeoutMs);
    if (err != ESP_OK) {
        return err;
    }

    if (bytesRead == 0) {
        return ESP_OK;
    }

    const size_t bytesWritten = fwrite(buffer.data(), 1, bytesRead, activeFile);
    if (bytesWritten != bytesRead) {
        return ESP_FAIL;
    }

    recordedDataBytes += static_cast<uint32_t>(bytesWritten);
    return ESP_OK;
}

esp_err_t SoundRecorder::stopRecording() {
    if (!recording) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    const std::string completedPath = currentRecordingPath;

    if (activeFile != nullptr) {
        if (fseek(activeFile, 0, SEEK_SET) != 0 ||
            !writeWavHeader(activeFile,
                            activeSampleRate,
                            activeChannels,
                            activeBitsPerSample,
                            recordedDataBytes)) {
            result = ESP_FAIL;
        }

        if (fclose(activeFile) != 0 && result == ESP_OK) {
            result = ESP_FAIL;
        }
    } else {
        result = ESP_FAIL;
    }

    activeFile = nullptr;
    microphone->deinitialize();
    recording = false;

    if (result == ESP_OK) {
        lastCompletedRecordingPath = completedPath;
        ESP_LOGI(TAG, "Recorded %u bytes to %s",
                 static_cast<unsigned>(recordedDataBytes),
                 completedPath.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to finalize recording: %s", completedPath.c_str());
    }

    activeSampleRate = 0;
    activeChannels = 0;
    activeBitsPerSample = 0;
    recordedDataBytes = 0;
    currentRecordingPath.clear();

    return result;
}

esp_err_t SoundRecorder::recordToFile(const std::string& fileName,
                                      uint32_t durationMs,
                                      uint32_t sampleRate,
                                      uint8_t channels,
                                      uint8_t bitsPerSample) {
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (durationMs == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = startRecordingToFile(fileName, sampleRate, channels, bitsPerSample);
    if (err != ESP_OK) {
        return err;
    }

    const int64_t startMs = esp_timer_get_time() / 1000;
    const int64_t deadlineMs = startMs + durationMs;

    while ((esp_timer_get_time() / 1000) < deadlineMs) {
        err = processRecordingChunk(200);
        if (err != ESP_OK) {
            stopRecording();
            return err;
        }
    }

    return stopRecording();
}

std::string SoundRecorder::resolveRecordingPath(const std::string& fileName) const {
    if (!fileName.empty() && fileName[0] == '/') {
        return fileName;
    }
    return sdCard->getRecordingsDir() + "/" + fileName;
}

bool SoundRecorder::writeWavHeader(FILE* file,
                                   uint32_t sampleRate,
                                   uint8_t channels,
                                   uint8_t bitsPerSample,
                                   uint32_t dataBytes) const {
    if (file == nullptr) {
        return false;
    }

    const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
    const uint32_t riffSize = 36 + dataBytes;

    std::array<uint8_t, 44> header = {};
    std::memcpy(header.data(), "RIFF", 4);
    writeLe32(header.data() + 4, riffSize);
    std::memcpy(header.data() + 8, "WAVE", 4);
    std::memcpy(header.data() + 12, "fmt ", 4);
    writeLe32(header.data() + 16, 16);  // fmt chunk size
    writeLe16(header.data() + 20, 1);   // PCM
    writeLe16(header.data() + 22, channels);
    writeLe32(header.data() + 24, sampleRate);
    writeLe32(header.data() + 28, byteRate);
    writeLe16(header.data() + 32, blockAlign);
    writeLe16(header.data() + 34, bitsPerSample);
    std::memcpy(header.data() + 36, "data", 4);
    writeLe32(header.data() + 40, dataBytes);

    return fwrite(header.data(), 1, header.size(), file) == header.size();
}
