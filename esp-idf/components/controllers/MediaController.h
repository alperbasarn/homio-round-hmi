#pragma once

#include <cstdint>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "SoundPlayer.h"
#include "SoundRecorder.h"
#include "esp_err.h"

class MediaController {
public:
    MediaController(SoundRecorder* recorder, SoundPlayer* player);
    ~MediaController();

    esp_err_t initialize();
    esp_err_t startSoundRecord(const std::string& fileName = "");
    esp_err_t stopSoundRecord(uint32_t timeoutMs = 5000);
    esp_err_t playLastSoundRecord();
    esp_err_t requestSoundEffect(const std::string& fileName);
    esp_err_t requestTouchPressEffect();
    esp_err_t requestTouchReleaseEffect();

    static const char* touchPressEffectFileName();
    static const char* touchReleaseEffectFileName();

    bool isRecording() const { return recordingTaskRunning; }
    const std::string& getLastRecordingPath() const { return lastRecordingPath; }

private:
    struct SoundEffectRequest {
        char fileName[96];
    };

    static void recordingTaskEntry(void* arg);
    static void soundEffectTaskEntry(void* arg);
    void recordingTaskLoop();
    void soundEffectTaskLoop();
    std::string generateRecordingFileName() const;

    SoundRecorder* soundRecorder;
    SoundPlayer* soundPlayer;

    bool initialized;
    bool recorderReady;
    volatile bool stopRecordingRequested;
    volatile bool recordingTaskRunning;
    TaskHandle_t recordingTaskHandle;

    volatile bool soundEffectTaskRunning;
    TaskHandle_t soundEffectTaskHandle;
    QueueHandle_t soundEffectQueue;

    esp_err_t lastRecordingError;
    std::string activeRecordingPath;
    std::string lastRecordingPath;
};
