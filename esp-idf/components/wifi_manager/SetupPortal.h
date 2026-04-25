#pragma once

#include <string>
#include <atomic>
#include <functional>

#include "esp_err.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"

class WiFiManager;
class NVSManager;

class SetupPortal {
public:
    SetupPortal(WiFiManager* wifiManager, NVSManager* nvsManager);
    ~SetupPortal();

    esp_err_t start();
    void stop();
    void setScreenControlCallback(std::function<bool(const std::string&)> callback);
    void setScreenStatusCallback(std::function<std::string(void)> callback);
    void setOtaConfigUpdatedCallback(std::function<void(void)> callback);
    void setOtaStatusCallback(std::function<std::string(void)> callback);
    void setOtaActionCallback(std::function<esp_err_t(const std::string&)> callback);
    void setDeviceInfoStatusCallback(std::function<std::string(void)> callback);

private:
    WiFiManager* wifiManager;
    NVSManager* nvsManager;

    httpd_handle_t httpServer;
    std::atomic<bool> dnsRunning;
    int dnsSocket;
    std::function<bool(const std::string&)> screenControlCallback;
    std::function<std::string(void)> screenStatusCallback;
    std::function<void(void)> otaConfigUpdatedCallback;
    std::function<std::string(void)> otaStatusCallback;
    std::function<esp_err_t(const std::string&)> otaActionCallback;
    std::function<std::string(void)> deviceInfoStatusCallback;

    static constexpr int HTTP_PORT = 80;
    static constexpr int DNS_PORT = 53;

    esp_err_t startHttpServer();
    void stopHttpServer();
    esp_err_t startDnsServer();
    void stopDnsServer();

    static void dnsTask(void* arg);
    void dnsLoop();
    bool sendDnsResponse(const uint8_t* request, size_t reqLen, const sockaddr_in& clientAddr, socklen_t clientLen);

    static esp_err_t rootGetHandler(httpd_req_t* req);
    static esp_err_t deviceInfoPageGetHandler(httpd_req_t* req);
    static esp_err_t statusGetHandler(httpd_req_t* req);
    static esp_err_t deviceInfoStatusGetHandler(httpd_req_t* req);
    static esp_err_t scanGetHandler(httpd_req_t* req);
    static esp_err_t devicePostHandler(httpd_req_t* req);
    static esp_err_t staticIpPostHandler(httpd_req_t* req);
    static esp_err_t screenControlPostHandler(httpd_req_t* req);
    static esp_err_t otaPostHandler(httpd_req_t* req);
    static esp_err_t wifiPostHandler(httpd_req_t* req);
    static esp_err_t mqttPostHandler(httpd_req_t* req);
    static esp_err_t weatherPostHandler(httpd_req_t* req);
    static esp_err_t timePostHandler(httpd_req_t* req);
    static esp_err_t resetPostHandler(httpd_req_t* req);
    static esp_err_t captiveRedirectHandler(httpd_req_t* req);

    std::string renderRootPage() const;
    std::string renderDeviceInfoPage() const;
    std::string renderStatusJson() const;
    std::string renderDeviceInfoStatusJson() const;
    std::string renderScanJson() const;

    static std::string urlDecode(const std::string& value);
    static std::string jsonEscape(const std::string& value);
    static std::string htmlEscape(const std::string& value);
    static std::string getFormValue(const std::string& body, const std::string& key);
    static std::string readBody(httpd_req_t* req);

    esp_err_t saveDeviceFromForm(const std::string& body, std::string& responseJson);
    esp_err_t saveStaticIpFromCurrentConnection(std::string& responseJson);
    esp_err_t saveScreenControlFromForm(const std::string& body, std::string& responseJson);
    esp_err_t saveOtaFromForm(const std::string& body, std::string& responseJson);
    esp_err_t saveWifiFromForm(const std::string& body, std::string& responseJson);
    esp_err_t saveMqttFromForm(const std::string& body, std::string& responseJson);
    esp_err_t saveWeatherFromForm(const std::string& body, std::string& responseJson);
    esp_err_t saveTimeFromForm(const std::string& body, std::string& responseJson);
};
