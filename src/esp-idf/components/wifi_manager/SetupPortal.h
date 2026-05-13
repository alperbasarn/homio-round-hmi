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
    void onStaGotIp();
    void setCommandCallback(std::function<void(const std::string&)> callback);

private:
    WiFiManager* wifiManager;
    NVSManager* nvsManager;

    httpd_handle_t httpServer;
    std::atomic<bool> dnsRunning;
    int dnsSocket;
    std::function<void(const std::string&)> commandCallback;

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
    static esp_err_t statusGetHandler(httpd_req_t* req);
    static esp_err_t wifiCredentialGetHandler(httpd_req_t* req);
    static esp_err_t wifiCredentialPostHandler(httpd_req_t* req);
    static esp_err_t wifiEnabledPostHandler(httpd_req_t* req);   // T-18
    static esp_err_t portalEnabledPostHandler(httpd_req_t* req); // T-19
    static esp_err_t factoryResetPostHandler(httpd_req_t* req);
    static esp_err_t captiveRedirectHandler(httpd_req_t* req);

    std::string renderRootPage() const;
    std::string renderStatusJson() const;
    std::string renderWifiCredentialsJson() const;

    esp_err_t saveWifiCredentialFromForm(const std::string& body, std::string& responseJson);
    esp_err_t saveWifiEnabledFromForm(const std::string& body, std::string& responseJson);    // T-18
    esp_err_t savePortalEnabledFromForm(const std::string& body, std::string& responseJson);  // T-19

    static std::string urlDecode(const std::string& value);
    static std::string jsonEscape(const std::string& value);
    static std::string htmlEscape(const std::string& value);
    static std::string getFormValue(const std::string& body, const std::string& key);
    static std::string readBody(httpd_req_t* req);
};
