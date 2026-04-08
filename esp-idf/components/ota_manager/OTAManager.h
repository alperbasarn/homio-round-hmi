#pragma once

#include <atomic>
#include <string>

#include "esp_err.h"

class WiFiManager;

class OTAManager {
public:
    explicit OTAManager(WiFiManager* wifiManager);
    ~OTAManager();

    esp_err_t startUpdate(const std::string& url, bool rebootOnSuccess = true);
    bool isBusy() const { return busy.load(); }
    esp_err_t getLastError() const { return lastError; }

    void setServerCert(const char* certPem);
    void setAllowInsecure(bool allow);

    std::string getRunningVersion() const;
    std::string getRunningPartitionLabel() const;

private:
    struct OtaTaskArgs {
        OTAManager* self;
        std::string url;
        bool reboot;
    };

    WiFiManager* wifiManager;
    const char* serverCertPem;
    bool allowInsecure;
    std::atomic<bool> busy;
    esp_err_t lastError;

    static void otaTaskEntry(void* arg);
    void otaTask(const std::string& url, bool reboot);
    bool isNetworkReady() const;
};
