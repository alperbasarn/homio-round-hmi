#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <atomic>
#include <string>

#include "esp_err.h"

class WiFiManager;

struct OtaReleaseInfo {
    bool configured = false;
    bool busy = false;
    bool updateAvailable = false;
    std::string currentVersion;
    std::string availableVersion;
    std::string statusMessage;
    std::string manifestUrl;
    esp_err_t lastError = ESP_OK;
};

class OTAManager {
public:
    explicit OTAManager(WiFiManager* wifiManager);
    ~OTAManager();

    esp_err_t startUpdate(const std::string& url, bool rebootOnSuccess = true);
    esp_err_t startReleaseUpdate(bool rebootOnSuccess = true);
    bool isBusy() const { return busy.load(); }
    esp_err_t getLastError() const { return lastError; }

    void setServerCert(const char* certPem);
    void setAllowInsecure(bool allow);
    void setManifestUrl(const std::string& url);
    void setDeviceVariantId(const std::string& variantId);

    std::string getRunningVersion() const;
    std::string getRunningPartitionLabel() const;
    OtaReleaseInfo getReleaseInfo() const;

private:
    struct OtaTaskArgs {
        OTAManager* self;
        std::string url;
        bool reboot;
    };

    struct ReleaseTaskArgs {
        OTAManager* self;
        bool reboot;
    };

    WiFiManager* wifiManager;
    const char* serverCertPem;
    bool allowInsecure;
    std::atomic<bool> busy;
    esp_err_t lastError;
    std::string manifestUrl;
    std::string deviceVariantId;
    std::string availableVersion;
    std::string statusMessage;
    bool updateAvailable;
    SemaphoreHandle_t stateMutex;

    static void otaTaskEntry(void* arg);
    static void releaseTaskEntry(void* arg);
    void otaTask(const std::string& url, bool reboot);
    void releaseTask(bool reboot);
    esp_err_t performOta(const std::string& url, bool reboot);
    esp_err_t fetchManifest(std::string& outVersion, std::string& outUrl);
    bool isNetworkReady() const;
    void updateReleaseState(const std::string& nextAvailableVersion,
                            const std::string& nextStatusMessage,
                            bool nextUpdateAvailable,
                            esp_err_t nextError);
};
