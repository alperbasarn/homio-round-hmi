#include "MediaController.h"

#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace {
constexpr const char* TAG = "MediaController";
constexpr uint32_t DEFAULT_SAMPLE_RATE = 16000;
constexpr uint8_t DEFAULT_CHANNELS = 1;
constexpr uint8_t DEFAULT_BITS_PER_SAMPLE = 16;
constexpr uint32_t RECORD_TASK_STACK_SIZE = 4096;
constexpr UBaseType_t RECORD_TASK_PRIORITY = 4;
constexpr uint32_t SFX_TASK_STACK_SIZE = 8192;
constexpr UBaseType_t SFX_TASK_PRIORITY = 3;
constexpr UBaseType_t SFX_QUEUE_LENGTH = 12;
constexpr const char* TOUCH_PRESS_EFFECT_FILE = "touch_press.mp3";
constexpr const char* TOUCH_RELEASE_EFFECT_FILE = "touch_release.mp3";
}  // namespace

MediaController::MediaController(SoundRecorder* recorder, SoundPlayer* player)
    : soundRecorder(recorder),
      soundPlayer(player),
      initialized(false),
      recorderReady(false),
      stopRecordingRequested(false),
      recordingTaskRunning(false),
      recordingTaskHandle(nullptr),
      soundEffectTaskRunning(false),
      soundEffectTaskHandle(nullptr),
      soundEffectQueue(nullptr),
      lastRecordingError(ESP_OK),
      activeRecordingPath(""),
      lastRecordingPath("") {
}

MediaController::~MediaController() {
    if (recordingTaskRunning) {
        stopSoundRecord(2000);
    }

    if (soundEffectTaskRunning) {
        soundEffectTaskRunning = false;
        if (soundEffectQueue != nullptr) {
            SoundEffectRequest request = {};
            xQueueSend(soundEffectQueue, &request, 0);
        }
        const int64_t deadlineMs = (esp_timer_get_time() / 1000) + 1000;
        while (soundEffectTaskHandle != nullptr &&
               (esp_timer_get_time() / 1000) < deadlineMs) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (soundEffectQueue != nullptr) {
        vQueueDelete(soundEffectQueue);
        soundEffectQueue = nullptr;
    }
}

esp_err_t MediaController::initialize() {
    if (initialized) {
        return ESP_OK;
    }
    if (soundPlayer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    recorderReady = false;
    if (soundRecorder != nullptr) {
        esp_err_t recErr = soundRecorder->initialize();
        if (recErr == ESP_OK) {
            recorderReady = true;
        } else {
            ESP_LOGW(TAG, "SoundRecorder init failed (%s). Recording disabled; sound effects still enabled.",
                     esp_err_to_name(recErr));
        }
    }

    esp_err_t err = soundPlayer->initialize();
    if (err != ESP_OK) {
        return err;
    }

    if (soundEffectQueue == nullptr) {
        soundEffectQueue = xQueueCreate(SFX_QUEUE_LENGTH, sizeof(SoundEffectRequest));
        if (soundEffectQueue == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    soundEffectTaskRunning = true;
    BaseType_t taskCreated = xTaskCreate(&MediaController::soundEffectTaskEntry,
                                         "MediaSfxTask",
                                         SFX_TASK_STACK_SIZE,
                                         this,
                                         SFX_TASK_PRIORITY,
                                         &soundEffectTaskHandle);
    if (taskCreated != pdPASS) {
        soundEffectTaskRunning = false;
        soundEffectTaskHandle = nullptr;
        return ESP_FAIL;
    }

    initialized = true;
    return ESP_OK;
}

esp_err_t MediaController::startSoundRecord(const std::string& fileName) {
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!recorderReady) {
        return ESP_ERR_INVALID_STATE;
    }
    if (recordingTaskRunning) {
        return ESP_ERR_INVALID_STATE;
    }

    std::string resolvedFileName = fileName;
    if (resolvedFileName.empty()) {
        resolvedFileName = generateRecordingFileName();
    }
    const bool hasWavExtension =
        (resolvedFileName.size() >= 4) &&
        (resolvedFileName.rfind(".wav") == resolvedFileName.size() - 4);
    if (resolvedFileName.find('/') == std::string::npos && !hasWavExtension) {
        resolvedFileName += ".wav";
    }

    esp_err_t err = soundRecorder->startRecordingToFile(resolvedFileName,
                                                         DEFAULT_SAMPLE_RATE,
                                                         DEFAULT_CHANNELS,
                                                         DEFAULT_BITS_PER_SAMPLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start recorder: %s", esp_err_to_name(err));
        return err;
    }

    stopRecordingRequested = false;
    recordingTaskRunning = true;
    lastRecordingError = ESP_OK;
    activeRecordingPath = soundRecorder->getCurrentRecordingPath();
    if (soundEffectQueue != nullptr) {
        xQueueReset(soundEffectQueue);
    }

    BaseType_t taskCreated = xTaskCreate(&MediaController::recordingTaskEntry,
                                         "MediaRecTask",
                                         RECORD_TASK_STACK_SIZE,
                                         this,
                                         RECORD_TASK_PRIORITY,
                                         &recordingTaskHandle);
    if (taskCreated != pdPASS) {
        recordingTaskRunning = false;
        recordingTaskHandle = nullptr;
        stopRecordingRequested = true;
        soundRecorder->stopRecording();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Recording started: %s", activeRecordingPath.c_str());
    return ESP_OK;
}

esp_err_t MediaController::stopSoundRecord(uint32_t timeoutMs) {
    if (!recorderReady) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!recordingTaskRunning) {
        return ESP_ERR_INVALID_STATE;
    }

    stopRecordingRequested = true;
    const int64_t deadlineMs = (esp_timer_get_time() / 1000) + timeoutMs;
    while (recordingTaskRunning && ((esp_timer_get_time() / 1000) < deadlineMs)) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (recordingTaskRunning) {
        return ESP_ERR_TIMEOUT;
    }

    return lastRecordingError;
}

esp_err_t MediaController::playLastSoundRecord() {
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!recorderReady) {
        return ESP_ERR_INVALID_STATE;
    }
    if (recordingTaskRunning) {
        return ESP_ERR_INVALID_STATE;
    }

    if (lastRecordingPath.empty()) {
        lastRecordingPath = soundRecorder->getLastCompletedRecordingPath();
    }
    if (lastRecordingPath.empty()) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Playing last recording: %s", lastRecordingPath.c_str());
    return soundPlayer->playFile(lastRecordingPath);
}

esp_err_t MediaController::requestSoundEffect(const std::string& fileName) {
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (fileName.empty()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (recordingTaskRunning) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!soundEffectTaskRunning || soundEffectQueue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    SoundEffectRequest request = {};
    std::strncpy(request.fileName, fileName.c_str(), sizeof(request.fileName) - 1);
    if (uxQueueSpacesAvailable(soundEffectQueue) == 0) {
        SoundEffectRequest dropped = {};
        if (xQueueReceive(soundEffectQueue, &dropped, 0) == pdTRUE) {
            ESP_LOGW(TAG, "SFX queue full, dropped oldest effect: %s", dropped.fileName);
        }
    }
    if (xQueueSend(soundEffectQueue, &request, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "SFX queue full, dropping effect: %s", request.fileName);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t MediaController::requestTouchPressEffect() {
    return requestSoundEffect(touchPressEffectFileName());
}

esp_err_t MediaController::requestTouchReleaseEffect() {
    return requestSoundEffect(touchReleaseEffectFileName());
}

const char* MediaController::touchPressEffectFileName() {
    return TOUCH_PRESS_EFFECT_FILE;
}

const char* MediaController::touchReleaseEffectFileName() {
    return TOUCH_RELEASE_EFFECT_FILE;
}

void MediaController::recordingTaskEntry(void* arg) {
    auto* self = static_cast<MediaController*>(arg);
    if (self != nullptr) {
        self->recordingTaskLoop();
    }
    vTaskDelete(nullptr);
}

void MediaController::recordingTaskLoop() {
    while (!stopRecordingRequested) {
        esp_err_t err = soundRecorder->processRecordingChunk(200);
        if (err != ESP_OK) {
            lastRecordingError = err;
            ESP_LOGE(TAG, "Recording chunk failed: %s", esp_err_to_name(err));
            break;
        }
    }

    esp_err_t stopErr = soundRecorder->stopRecording();
    if (stopErr != ESP_OK && lastRecordingError == ESP_OK) {
        lastRecordingError = stopErr;
    }

    if (lastRecordingError == ESP_OK) {
        lastRecordingPath = activeRecordingPath;
        ESP_LOGI(TAG, "Recording completed: %s", lastRecordingPath.c_str());
    }

    activeRecordingPath.clear();
    recordingTaskHandle = nullptr;
    recordingTaskRunning = false;
}

void MediaController::soundEffectTaskEntry(void* arg) {
    auto* self = static_cast<MediaController*>(arg);
    if (self != nullptr) {
        self->soundEffectTaskLoop();
    }
    vTaskDelete(nullptr);
}

void MediaController::soundEffectTaskLoop() {
    SoundEffectRequest request = {};
    while (soundEffectTaskRunning) {
        if (xQueueReceive(soundEffectQueue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!soundEffectTaskRunning) {
            break;
        }
        if (request.fileName[0] == '\0') {
            continue;
        }
        if (recordingTaskRunning) {
            ESP_LOGW(TAG, "Skipping sound effect while recording: %s", request.fileName);
            continue;
        }

        esp_err_t err = soundPlayer->playEffectSound(request.fileName);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to play effect '%s': %s",
                     request.fileName,
                     esp_err_to_name(err));
        }
    }

    soundEffectTaskHandle = nullptr;
}

std::string MediaController::generateRecordingFileName() const {
    const int64_t ms = esp_timer_get_time() / 1000;
    return "recording_" + std::to_string(ms) + ".wav";
}
