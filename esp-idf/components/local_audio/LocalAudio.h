#pragma once

#include <cstdint>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "SoundPlayer.h"
#include "SoundRecorder.h"
#include "esp_err.h"

// PERI-11: the on-board audio (touch and swipe effects, the recorder) behind one small surface;
// the transports and screens never touch SoundPlayer or SoundRecorder directly.
class LocalAudio {
public:
    LocalAudio(SoundRecorder* recorder, SoundPlayer* player);
    ~LocalAudio();

    esp_err_t initialize();
    esp_err_t startSoundRecord(const std::string& fileName = "");
    esp_err_t stopSoundRecord(uint32_t timeoutMs = 5000);
    esp_err_t playLastSoundRecord();
    esp_err_t requestSoundEffect(const std::string& fileName);
    esp_err_t requestTouchPressEffect();
    esp_err_t requestTouchReleaseEffect();



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
