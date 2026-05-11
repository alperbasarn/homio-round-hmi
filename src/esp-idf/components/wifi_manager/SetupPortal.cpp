#include "SetupPortal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "WiFiManager.h"
#include "NVSManager.h"
#include "ConnectivityManager.h"

#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace {
constexpr const char* TAG = "SetupPortal";
constexpr size_t MAX_POST_BODY = 2048;
}

SetupPortal::SetupPortal(WiFiManager* wifiManagerValue, NVSManager* nvsManagerValue)
    : wifiManager(wifiManagerValue),
      nvsManager(nvsManagerValue),
      httpServer(nullptr),
      dnsRunning(false),
      dnsSocket(-1) {
}

SetupPortal::~SetupPortal() {
    stop();
}

void SetupPortal::setScreenControlCallback(std::function<bool(const std::string&)> callback) {
    screenControlCallback = std::move(callback);
}

void SetupPortal::setScreenStatusCallback(std::function<std::string(void)> callback) {
    screenStatusCallback = std::move(callback);
}

void SetupPortal::setOtaConfigUpdatedCallback(std::function<void(void)> callback) {
    otaConfigUpdatedCallback = std::move(callback);
}

void SetupPortal::setOtaStatusCallback(std::function<std::string(void)> callback) {
    otaStatusCallback = std::move(callback);
}

void SetupPortal::setOtaActionCallback(std::function<esp_err_t(const std::string&)> callback) {
    otaActionCallback = std::move(callback);
}

void SetupPortal::setDeviceInfoStatusCallback(std::function<std::string(void)> callback) {
    deviceInfoStatusCallback = std::move(callback);
}

void SetupPortal::setCommandCallback(std::function<void(const std::string&)> callback) {
    commandCallback = std::move(callback);
}

void SetupPortal::setBtScanResultsCallback(std::function<std::string(void)> callback) {
    btScanResultsCallback = std::move(callback);
}

esp_err_t SetupPortal::start() {
    esp_err_t err = startHttpServer();
    if (err != ESP_OK) {
        return err;
    }
    return startDnsServer();
}

void SetupPortal::stop() {
    stopDnsServer();
    stopHttpServer();
}

void SetupPortal::onStaGotIp() {
    // No-op: httpd_start with INADDR_ANY already binds to both netifs.
    // Restarting the server here caused captive-portal popup failures and
    // empty form fields on first load (T-10).
    ESP_LOGI(TAG, "STA got IP - HTTP server left running (INADDR_ANY)"elds on first load (T-10).
    ESP_LOGI(TAG, "STA got IP - HTTP server left running (INADDR_ANY)");
}

esp_err_t SetupPortal::startHttpServer() {
    if (httpServer != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    config.ctrl_port = HTTP_PORT + 1;
    config.max_uri_handlers = 60;
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&httpServer, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        httpServer = nullptr;
        return ESP_FAIL;
    }

    httpd_uri_t root = {};
    root.uri = "/";
    root.method = HTTP_GET;
    root.handler = rootGetHandler;
    root.user_ctx = this;
    httpd_register_uri_handler(httpServer, &root);

    httpd_uri_t status = {};
    status.uri = "/api/status";
    status.method = HTTP_GET;
    status.handler = statusGetHandler;
    status.user_ctx = this;
    httpd_register_uri_handler(httpServer, &status);

    httpd_uri_t deviceInfoPage = {};
    deviceInfoPage.uri = "/device-info";
    deviceInfoPage.method = HTTP_GET;
    deviceInfoPage.handler = deviceInfoPageGetHandler;
    deviceInfoPage.user_ctx = this;
    httpd_register_uri_handler(httpServer, &deviceInfoPage);

    httpd_uri_t deviceInfoStatus = {};
    deviceInfoStatus.uri = "/api/device-info";
    deviceInfoStatus.method = HTTP_GET;
    deviceInfoStatus.handler = deviceInfoStatusGetHandler;
    deviceInfoStatus.user_ctx = this;
    httpd_register_uri_handler(httpServer, &deviceInfoStatus);

    httpd_uri_t scan = {};
    scan.uri = "/api/scan";
    scan.method = HTTP_GET;
    scan.handler = scanGetHandler;
    scan.user_ctx = this;
    httpd_register_uri_handler(httpServer, &scan);

    httpd_uri_t devicePost = {};
    devicePost.uri = "/api/device";
    devicePost.method = HTTP_POST;
    devicePost.handler = devicePostHandler;
    devicePost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &devicePost);

    httpd_uri_t staticIpPost = {};
    staticIpPost.uri = "/api/static-ip/current";
    staticIpPost.method = HTTP_POST;
    staticIpPost.handler = staticIpPostHandler;
    staticIpPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &staticIpPost);

    httpd_uri_t screenControlPost = {};
    screenControlPost.uri = "/api/control/screen";
    screenControlPost.method = HTTP_POST;
    screenControlPost.handler = screenControlPostHandler;
    screenControlPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &screenControlPost);

    httpd_uri_t brightnessControlPost = {};
    brightnessControlPost.uri = "/api/control/brightness";
    brightnessControlPost.method = HTTP_POST;
    brightnessControlPost.handler = brightnessControlPostHandler;
    brightnessControlPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &brightnessControlPost);

    httpd_uri_t bluetoothControlPost = {};
    bluetoothControlPost.uri = "/api/control/bluetooth";
    bluetoothControlPost.method = HTTP_POST;
    bluetoothControlPost.handler = bluetoothControlPostHandler;
    bluetoothControlPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &bluetoothControlPost);

    httpd_uri_t bluetoothScanGet = {};
    bluetoothScanGet.uri = "/api/bluetooth/scan";
    bluetoothScanGet.method = HTTP_GET;
    bluetoothScanGet.handler = bluetoothScanGetHandler;
    bluetoothScanGet.user_ctx = this;
    httpd_register_uri_handler(httpServer, &bluetoothScanGet);

    httpd_uri_t otaPost = {};
    otaPost.uri = "/api/ota";
    otaPost.method = HTTP_POST;
    otaPost.handler = otaPostHandler;
    otaPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &otaPost);

    httpd_uri_t wifiPost = {};
    wifiPost.uri = "/api/wifi";
    wifiPost.method = HTTP_POST;
    wifiPost.handler = wifiPostHandler;
    wifiPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &wifiPost);

    httpd_uri_t mqttPost = {};
    mqttPost.uri = "/api/mqtt";
    mqttPost.method = HTTP_POST;
    mqttPost.handler = mqttPostHandler;
    mqttPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &mqttPost);

    httpd_uri_t weatherPost = {};
    weatherPost.uri = "/api/weather";
    weatherPost.method = HTTP_POST;
    weatherPost.handler = weatherPostHandler;
    weatherPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &weatherPost);

    httpd_uri_t timePost = {};
    timePost.uri = "/api/time";
    timePost.method = HTTP_POST;
    timePost.handler = timePostHandler;
    timePost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &timePost);

    httpd_uri_t wifiCredentialsGet = {};
    wifiCredentialsGet.uri = "/api/wifi/credentials";
    wifiCredentialsGet.method = HTTP_GET;
    wifiCredentialsGet.handler = wifiCredentialGetHandler;
    wifiCredentialsGet.user_ctx = this;
    httpd_register_uri_handler(httpServer, &wifiCredentialsGet);

    httpd_uri_t wifiCredentialsPost = {};
    wifiCredentialsPost.uri = "/api/wifi/credentials";
    wifiCredentialsPost.method = HTTP_POST;
    wifiCredentialsPost.handler = wifiCredentialPostHandler;
    wifiCredentialsPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &wifiCredentialsPost);

    httpd_uri_t bluetoothBondedGet = {};
    bluetoothBondedGet.uri = "/api/bluetooth/bonded";
    bluetoothBondedGet.method = HTTP_GET;
    bluetoothBondedGet.handler = bluetoothBondedDevicesGetHandler;
    bluetoothBondedGet.user_ctx = this;
    httpd_register_uri_handler(httpServer, &bluetoothBondedGet);

    httpd_uri_t bluetoothBondedDisconnect = {};
    bluetoothBondedDisconnect.uri = "/api/bluetooth/disconnect";
    bluetoothBondedDisconnect.method = HTTP_POST;
    bluetoothBondedDisconnect.handler = bluetoothDisconnectPostHandler;
    bluetoothBondedDisconnect.user_ctx = this;
    httpd_register_uri_handler(httpServer, &bluetoothBondedDisconnect);

    httpd_uri_t bluetoothBondedForget = {};
    bluetoothBondedForget.uri = "/api/bluetooth/forget";
    bluetoothBondedForget.method = HTTP_POST;
    bluetoothBondedForget.handler = bluetoothBondedDeviceForgetPostHandler;
    bluetoothBondedForget.user_ctx = this;
    httpd_register_uri_handler(httpServer, &bluetoothBondedForget);

    httpd_uri_t resetPost = {};
    resetPost.uri = "/api/reset";
    resetPost.method = HTTP_POST;
    resetPost.handler = resetPostHandler;
    resetPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &resetPost);

    httpd_uri_t resetGet = {};
    resetGet.uri = "/api/reset";
    resetGet.method = HTTP_GET;
    resetGet.handler = resetPostHandler;
    resetGet.user_ctx = this;
    httpd_register_uri_handler(httpServer, &resetGet);

    httpd_uri_t factoryResetPost = {};
    factoryResetPost.uri = "/api/factory-reset";
    factoryResetPost.method = HTTP_POST;
    factoryResetPost.handler = factoryResetPostHandler;
    factoryResetPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &factoryResetPost);

    const char* captiveUris[] = {
        "/generate_204",
        "/gen_204",
        "/hotspot-detect.html",
        "/mobile/status.php",
        "/library/test/success.html",
        "/success.txt",
        "/success.html",
        "/check_network_status.txt",
        "/canonical.html",
        "/redirect",
        "/connecttest.txt",
        "/ncsi.txt",
        "/fwlink",
    };
    for (const char* uri : captiveUris) {
        httpd_uri_t redirect = {};
        redirect.uri = uri;
        redirect.method = HTTP_GET;
        redirect.handler = captiveRedirectHandler;
        redirect.user_ctx = this;
        httpd_register_uri_handler(httpServer, &redirect);

        httpd_uri_t redirectHead = {};
        redirectHead.uri = uri;
        redirectHead.method = HTTP_HEAD;
        redirectHead.handler = captiveRedirectHandler;
        redirectHead.user_ctx = this;
        httpd_register_uri_handler(httpServer, &redirectHead);
    }

    httpd_uri_t catchAll = {};
    catchAll.uri = "/*";
    catchAll.method = HTTP_GET;
    catchAll.handler = captiveRedirectHandler;
    catchAll.user_ctx = this;
    httpd_register_uri_handler(httpServer, &catchAll);

    httpd_uri_t catchAllHead = {};
    catchAllHead.uri = "/*";
    catchAllHead.method = HTTP_HEAD;
    catchAllHead.handler = captiveRedirectHandler;
    catchAllHead.user_ctx = this;
    httpd_register_uri_handler(httpServer, &catchAllHead);

    ESP_LOGI(TAG, "Setup portal HTTP server started");
    return ESP_OK;
}

void SetupPortal::stopHttpServer() {
    if (httpServer != nullptr) {
        httpd_stop(httpServer);
        httpServer = nullptr;
    }
}

esp_err_t SetupPortal::startDnsServer() {
    if (dnsRunning.load()) {
        return ESP_OK;
    }

    dnsRunning.store(true);
    if (xTaskCreate(&SetupPortal::dnsTask, "PortalDNS", 4096, this, 4, nullptr) != pdPASS) {
        dnsRunning.store(false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void SetupPortal::stopDnsServer() {
    if (!dnsRunning.load()) {
        return;
    }

    dnsRunning.store(false);
    if (dnsSocket >= 0) {
        shutdown(dnsSocket, 0);
        close(dnsSocket);
        dnsSocket = -1;
    }
}

void SetupPortal::dnsTask(void* arg) {
    auto* self = static_cast<SetupPortal*>(arg);
    if (self != nullptr) {
        self->dnsLoop();
    }
    vTaskDelete(nullptr);
}

void SetupPortal::dnsLoop() {
    dnsSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dnsSocket < 0) {
        dnsRunning.store(false);
        return;
    }

    sockaddr_in bindAddr = {};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons(DNS_PORT);

    if (bind(dnsSocket, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
        close(dnsSocket);
        dnsSocket = -1;
        dnsRunning.store(false);
        return;
    }

    struct timeval timeout = {};
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(dnsSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    uint8_t request[512];
    while (dnsRunning.load()) {
        sockaddr_in clientAddr = {};
        socklen_t clientLen = sizeof(clientAddr);
        int len = recvfrom(dnsSocket,
                           reinterpret_cast<char*>(request),
                           sizeof(request),
                           0,
                           reinterpret_cast<sockaddr*>(&clientAddr),
                           &clientLen);
        if (len <= 0) {
            continue;
        }

        sendDnsResponse(request, static_cast<size_t>(len), clientAddr, clientLen);
    }

    close(dnsSocket);
    dnsSocket = -1;
}

bool SetupPortal::sendDnsResponse(const uint8_t* request,
                                  size_t reqLen,
                                  const sockaddr_in& clientAddr,
                                  socklen_t clientLen) {
    if (reqLen < 12 || dnsSocket < 0) {
        return false;
    }

    const uint16_t qdcount = static_cast<uint16_t>((request[4] << 8) | request[5]);
    if (qdcount == 0) {
        return false;
    }

    // Walk the question name labels to find where QTYPE/QCLASS sit.
    size_t nameEnd = 12;
    while (nameEnd < reqLen && request[nameEnd] != 0) {
        nameEnd += static_cast<size_t>(request[nameEnd]) + 1;
    }
    // nameEnd now points at the null label; we need 4 more bytes (QTYPE + QCLASS).
    if (nameEnd + 4 >= reqLen) {
        return false;
    }

    // Only answer A-record queries (type 1).  For everything else (AAAA, MX, …)
    // return NOERROR with zero answers so the client falls back to IPv4.
    const uint16_t qtype = static_cast<uint16_t>((request[nameEnd + 1] << 8) | request[nameEnd + 2]);
    const size_t questionEnd = nameEnd + 5;  // null label + QTYPE(2) + QCLASS(2)

    uint8_t response[512] = {0};

    // Common header fields for both paths.
    response[0] = request[0];   // transaction ID
    response[1] = request[1];
    response[2] = 0x81;         // QR=1, OPCODE=0, AA=1, TC=0, RD=1
    response[3] = 0x80;         // RA=1, RCODE=0 (NOERROR)
    response[4] = request[4];   // QDCOUNT (echo)
    response[5] = request[5];
    // Copy question section verbatim.
    if (questionEnd > 12 && questionEnd <= sizeof(response)) {
        memcpy(response + 12, request + 12, questionEnd - 12);
    }

    if (qtype != 1 /* A */) {
        // NOERROR, ANCOUNT=0 — tells the client there is no such record type.
        response[6] = 0x00;
        response[7] = 0x00;
        sendto(dnsSocket,
               reinterpret_cast<const char*>(response),
               static_cast<int>(questionEnd),
               0,
               reinterpret_cast<const sockaddr*>(&clientAddr),
               clientLen);
        return true;
    }

    // A-record answer: resolve every hostname to the AP's IP.
    response[6] = 0x00;
    response[7] = 0x01;  // ANCOUNT = 1

    if (questionEnd + 16 > sizeof(response)) {
        return false;
    }

    size_t pos = questionEnd;
    response[pos++] = 0xC0; response[pos++] = 0x0C;  // name pointer → offset 12
    response[pos++] = 0x00; response[pos++] = 0x01;  // type A
    response[pos++] = 0x00; response[pos++] = 0x01;  // class IN
    response[pos++] = 0x00; response[pos++] = 0x00;
    response[pos++] = 0x00; response[pos++] = 0x3C;  // TTL 60 s
    response[pos++] = 0x00; response[pos++] = 0x04;  // RDLENGTH 4

    const std::string apIp = ConnectivityManager::instance().getSnapshot().ap_ip[0]
                             ? std::string(ConnectivityManager::instance().getSnapshot().ap_ip)
                             : std::string("192.168.4.1");
    struct in_addr addr = {};
    if (inet_pton(AF_INET, apIp.c_str(), &addr) != 1) {
        inet_pton(AF_INET, "192.168.4.1", &addr);
    }
    memcpy(response + pos, &addr.s_addr, 4);
    pos += 4;

    sendto(dnsSocket,
           reinterpret_cast<const char*>(response),
           static_cast<int>(pos),
           0,
           reinterpret_cast<const sockaddr*>(&clientAddr),
           clientLen);
    return true;
}

esp_err_t SetupPortal::rootGetHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "rootGetHandler called, free heap: %lu", esp_get_free_heap_size());
    const std::string html = self->renderRootPage();
    ESP_LOGI(TAG, "renderRootPage done, html size: %zu", html.size());
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html.c_str(), static_cast<ssize_t>(html.size()));
}

esp_err_t SetupPortal::statusGetHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }
    const std::string json = self->renderStatusJson();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t SetupPortal::deviceInfoPageGetHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }
    const std::string html = self->renderDeviceInfoPage();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html.c_str(), static_cast<ssize_t>(html.size()));
}

esp_err_t SetupPortal::deviceInfoStatusGetHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }
    const std::string json = self->renderDeviceInfoStatusJson();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t SetupPortal::scanGetHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }
    const std::string json = self->renderScanJson();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t SetupPortal::wifiPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->saveWifiFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::staticIpPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->saveStaticIpFromCurrentConnection(response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::screenControlPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->saveScreenControlFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::brightnessControlPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->saveBrightnessControlFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::bluetoothScanGetHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }
    std::string response;
    if (self->btScanResultsCallback) {
        response = self->btScanResultsCallback();
    } else {
        response = "{\"scanning\":false,\"devices\":[]}";
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::bluetoothControlPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->saveBluetoothControlFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::mqttPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->saveMqttFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::weatherPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->saveWeatherFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::timePostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->saveTimeFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::resetPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) return ESP_FAIL;

    const char* response = "{\"ok\":true,\"message\":\"Restarting device...\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    const esp_err_t sendErr = httpd_resp_send(req, response, static_cast<ssize_t>(strlen(response)));
    if (sendErr != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send reset response: %s", esp_err_to_name(sendErr));
    }

    if (self->commandCallback) {
        self->commandCallback("reset");
    } else {
        // Fallback when command handler is not wired.
        vTaskDelay(pdMS_TO_TICKS(120));
        esp_restart();
    }

    return ESP_OK;
}

esp_err_t SetupPortal::factoryResetPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) return ESP_FAIL;

    const char* response = "{\"ok\":true,\"message\":\"Factory reset started. Wiping NVS and restarting...\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    const esp_err_t sendErr = httpd_resp_send(req, response, static_cast<ssize_t>(strlen(response)));
    if (sendErr != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send factory reset response: %s", esp_err_to_name(sendErr));
    }

    if (self->commandCallback) {
        self->commandCallback("factoryReset");
    } else {
        ESP_LOGW(TAG, "Factory reset command callback not set; applying fallback reset path");
        if (self->nvsManager != nullptr) {
            self->nvsManager->clearAll();
        }
        vTaskDelay(pdMS_TO_TICKS(120));
        esp_restart();
    }

    return ESP_OK;
}

esp_err_t SetupPortal::captiveRedirectHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    const auto snap = ConnectivityManager::instance().getSnapshot();
    const std::string apIp = snap.ap_ip[0] ? std::string(snap.ap_ip) : std::string("192.168.4.1");
    const std::string location = "http://" + apIp + "/";

    // iOS, Android, and Windows captive portal detection all require an HTTP 302
    // redirect (not a 200 with HTML) to trigger the "sign in to network" popup.
    // A 200 response causes most OS captive portal detectors to conclude the
    // network has internet access and silently skip the captive portal notification.
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Location", location.c_str());
    return httpd_resp_send(req, nullptr, 0);
}

std::string SetupPortal::renderRootPage() const {
    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
         << "<title>QNOB Setup</title><style>"
     << "body{background:#000;color:#fff;font-family:monospace;margin:0;padding:14px;}"
     << "h1{font-size:19px;margin:0 0 14px;}h2{font-size:14px;margin:14px 0 7px;border-bottom:1px solid #333;padding-bottom:4px;}"
     << "form,section{border:1px solid #333;padding:10px;margin-bottom:10px;}"
     << "label{display:block;margin:7px 0 3px;color:#bbb;}"
     << "input,select{width:100%;background:#111;color:#fff;border:1px solid #555;padding:7px;box-sizing:border-box;margin-bottom:2px;}"
     << "button{width:100%;background:#000;color:#fff;border:1px solid #fff;padding:8px;cursor:pointer;margin-top:6px;}"
     << "button:hover{background:#222;}"
     << "small{display:block;margin-top:4px;color:#666;}pre{white-space:pre-wrap;word-break:break-word;border:1px solid #333;padding:8px;margin-top:10px;}"
     << ".portalLayout{display:grid;grid-template-columns:120px 130px minmax(0,1fr);gap:8px;align-items:start;}"
     << ".vtabs{display:flex;flex-direction:column;gap:6px;position:sticky;top:10px;}"
     << ".vtabs button{width:100%;padding:8px 10px;margin-top:0;text-align:left;}"
     << ".act{background:#fff!important;color:#000!important;border-color:#fff!important;}"
     << ".hidden{display:none!important;}"
     << ".og{display:grid;grid-template-columns:minmax(100px,auto) 1fr;gap:5px 10px;align-items:start;}"
     << ".og .k{color:#777;padding-top:2px;}.og .v{word-break:break-all;}"
     << ".slot{border:1px solid #333;padding:9px;margin-bottom:8px;border-radius:2px;}"
     << ".slot h3{margin:0 0 7px;font-size:12px;color:#888;}"
     << ".row{display:flex;gap:6px;align-items:center;}"
     << ".row button{width:auto;flex:1;}"
     << ".bditem{display:flex;align-items:center;gap:8px;border:1px solid #333;padding:8px;margin-bottom:5px;}"
     << ".bdinfo{flex:1;min-width:0;}.bdname{font-weight:bold;}.bdaddr{color:#666;font-size:11px;word-break:break-all;}"
     << ".ibtns{display:flex;gap:5px;}.ibtns button{width:auto;padding:5px 9px;margin-top:0;}"
     << ".danger{border-color:#a33!important;color:#f88!important;}"
     << ".pw-wrap{display:flex;gap:4px;align-items:center;margin-bottom:2px;}"
     << ".pw-wrap input{flex:1;margin:0;}"
     << ".pw-wrap button{width:auto;padding:5px 9px;margin-top:0;flex-shrink:0;font-size:11px;}"
     << ".bt-status{font-size:11px;padding:2px 7px;border-radius:2px;margin-left:6px;}"
     << ".bt-connected{color:#6f6;border:1px solid #6f6;}.bt-available{color:#9cc7ff;border:1px solid #9cc7ff;}"
     << "@media (max-width:900px){.portalLayout{grid-template-columns:1fr;}.vtabs{position:static;flex-direction:row;flex-wrap:wrap;} .vtabs button{text-align:center;}}"
     << "</style></head><body>"
     << "<h1>QNOB Setup Portal</h1>"
     << "<div class='portalLayout'>"

     // ── Primary tabs (left) ─────────────────────────────────────────────
     << "<div id='primaryTabs' class='vtabs'>"
     << "<button class='act' type='button' data-tab='tConfigure'>Configure</button>"
     << "<button type='button' data-tab='tControl'>Control</button>"
     << "</div>"

     // ── Sub-tabs (middle) ───────────────────────────────────────────────
     << "<div id='subTabNav'>"
     << "<div id='cfgTabs' class='vtabs'>"
     << "<button class='act' type='button' data-ctab='ctGeneral' data-parent='tConfigure'>General</button>"
     << "<button type='button' data-ctab='ctWifi' data-parent='tConfigure'>Wi-Fi</button>"
     << "<button type='button' data-ctab='ctBluetooth' data-parent='tConfigure'>Bluetooth</button>"
     << "<button type='button' data-ctab='ctApi' data-parent='tConfigure'>API</button>"
     << "</div>"
     << "<div id='ctrlTabs' class='vtabs hidden'>"
     << "<button class='act' type='button' data-ctrltab='ctrlScreen' data-parent='tControl'>Screen</button>"
     << "<button type='button' data-ctrltab='ctrlBrightness' data-parent='tControl'>Brightness</button>"
     << "</div>"
     << "</div>"

     // ── Content (right) ─────────────────────────────────────────────────
     << "<div id='contentPane'>"

     // ════════════════════════════════════════════════════════════════════
     // CONFIGURE TAB
     // ════════════════════════════════════════════════════════════════════
     << "<div id='tConfigure'>"

     // ── General ──────────────────────────────────────────────────────────
     << "<div id='ctGeneral'>"

     // Status overview
     << "<section><h2>Status</h2><div class='og'>"
     << "<div class='k'>Device</div><div class='v' id='ovDeviceName'>-</div>"
     << "<div class='k'>AP SSID</div><div class='v' id='ovApSsid'>-</div>"
     << "<div class='k'>AP Password</div><div class='v' id='ovApPassword'>-</div>"
     << "<div class='k'>AP IP</div><div class='v' id='ovApIp'>-</div>"
     << "<div class='k'>SW Version</div><div class='v' id='ovCurrentVersion'>-</div>"
     << "<div class='k'>Available</div><div class='v' id='ovAvailableVersion'>-</div>"
     << "<div class='k'>OTA Status</div><div class='v' id='ovOtaStatus'>-</div>"
     << "</div></section>"

     // Device form
     << "<form id='deviceForm'><h2>Device</h2>"
     << "<label>Device Suffix</label><input name='device_suffix' maxlength='4' placeholder='0000'>"
     << "<small>Device name is always Homio-&lt;suffix&gt; (A-Z and 0-9).</small>"
     << "<label>AP Password Protection</label><select id='apPwToggle' name='ap_password_enabled'><option value='off'>Disabled (open network)</option><option value='on'>Enabled</option></select>"
     << "<div id='apPwField' class='hidden'><label>Password (min 8 chars)</label><div class='pw-wrap'><input name='ap_password' id='apPwInput' type='password' placeholder='leave blank to keep current'><button type='button' onclick='pwToggle(this)'>Show</button></div><small>Enter new password or leave blank to keep existing.</small></div>"
     << "<button type='submit'>Save Device Settings</button>"
     << "</form>"

     // OTA form
     << "<form id='otaForm'><h2>OTA Update</h2>"
     << "<label>Variant</label><input name='variant' placeholder='esp32s3_lcd128'>"
     << "<small>Which release image this device is allowed to install.</small>"
     << "<label>Manifest URL (optional)</label><input name='manifest_url' placeholder='https://.../latest/{variant}.json'>"
     << "<small>Leave empty to use default server path; use {variant} placeholder.</small>"
     << "<div class='row'>"
     << "<button type='submit'>Save OTA Settings</button>"
     << "<button id='otaCheckBtn' type='button'>Check</button>"
     << "<button id='otaUpdateBtn' type='button' disabled>Update Now</button>"
     << "</div></form>"

     // Links / reset
     << "<section><h2>Device Control</h2>"
     << "<a href='/device-info' style='color:#9cc7ff;display:block;margin-bottom:8px;'>Open device info page</a>"
     << "<button id='restartBtn' type='button' class='danger'>Restart Device</button>"
     << "<button id='factoryResetBtn' type='button' class='danger' style='margin-top:8px;'>Factory Reset (Wipe All)</button>"
     << "</section>"
     << "</div>" // ctGeneral

     // ── Wi-Fi ─────────────────────────────────────────────────────────────
     << "<div id='ctWifi' class='hidden'>"

     // Current connection
     << "<section><h2>Current Connection</h2><div class='og'>"
     << "<div class='k'>Network</div><div class='v' id='ovStaSsid'>-</div>"
     << "<div class='k'>Signal</div><div class='v' id='ovSignal'>-</div>"
     << "<div class='k'>IP Address</div><div class='v' id='ovStaIp'>-</div>"
     << "</div>"
     << "</section>"

     // Credential slots (rendered by JS)
     << "<section><h2>Saved Credentials</h2><div id='wifiSlots'><small>Loading...</small></div></section>"

     // Scan / add
     << "<form id='wifiScanForm'><h2>Scan &amp; Connect</h2>"
     << "<label>Scan Results</label>"
     << "<select id='scanList'><option value='' selected disabled>Press Scan to search</option></select>"
     << "<label>Or enter SSID manually</label><input id='wifiSsidManual' placeholder='SSID'>"
     << "<label>Password</label><div class='pw-wrap'><input id='wifiPwInput' type='password' placeholder='Password'><button type='button' onclick='pwToggle(this)'>Show</button></div>"
     << "<label>Save to slot</label>"
     << "<select id='wifiSlotSelect'><option value='0'>Slot 1</option><option value='1'>Slot 2</option><option value='2'>Slot 3</option></select>"
     << "<div class='row'><button type='button' id='wifiScanBtn'>Scan</button><button type='button' id='wifiSaveBtn'>Save to Slot</button><button type='button' id='wifiConnectBtn'>Connect (no save)</button></div>"
     << "</form>"

     << "</div>" // ctWifi

     // ── Bluetooth ────────────────────────────────────────────────────────
     << "<div id='ctBluetooth' class='hidden'>"

     // BT settings form
     << "<form id='btForm'><h2>Bluetooth Settings</h2>"
     << "<label>Status: <span id='btStatus'>-</span></label>"
     << "<label>Enabled</label><select name='enabled'><option value='on'>On</option><option value='off'>Off</option></select>"
     << "<label>Device Name</label><input name='bt_name' maxlength='32' placeholder='Qnob PC Control'>"
     << "<small>Name change takes effect after restart.</small>"
     << "<button type='submit'>Apply</button></form>"

     // Bonded devices
     << "<section><h2>Bonded Devices</h2>"
     << "<div style='margin-bottom:6px;color:#888;'>Stored bonds: <strong id='btBondCount'>-</strong></div>"
     << "<div id='btBondedList'><small>Loading...</small></div>"
     << "<div class='row' style='margin-top:8px;'>"
     << "<button type='button' id='btRestartBtn'>Restart Adv.</button>"
     << "<button type='button' id='btClearBondsBtn' class='danger'>Clear All Bonds</button>"
     << "</div>"
     << "<small>If device doesn&apos;t appear on iPhone/PC, clear bonds then scan for it again.</small>"
     << "</section>"

     // BLE scan
     << "<section><h2>BLE Scan</h2>"
     << "<button type='button' id='btScanBtn'>Scan BLE Devices (5s)</button>"
     << "<div id='btScanStatus' style='margin-top:6px;color:#9cc7ff;'></div>"
     << "<div id='btScanResults' style='margin-top:6px;'></div>"
     << "</section>"

     << "</div>" // ctBluetooth

     // ── API ───────────────────────────────────────────────────────────────
     << "<div id='ctApi' class='hidden'>"

     << "<form id='mqttForm'><h2>MQTT</h2>"
     << "<label>Broker URL / IP</label><input name='url' id='mqttUrl'>"
     << "<label>Port</label><input name='port' id='mqttPort' value='8883'>"
     << "<label>Username</label><input name='username' id='mqttUsername'>"
     << "<label>Password</label><input name='password' type='password'>"
     << "<small>Saved to both sound and light MQTT configs.</small>"
     << "<button type='submit'>Save MQTT</button></form>"

     << "<form id='weatherForm'><h2>Weather</h2>"
     << "<label>City</label><input name='city' id='weatherCity'>"
     << "<label>Country Code</label><input name='country' id='weatherCountry'>"
     << "<label>API Token</label><input name='api_token' id='weatherToken'>"
     << "<button type='submit'>Save Weather</button></form>"

     << "<form id='timeForm'><h2>Time</h2>"
     << "<label>Time API Token</label><input name='api_token' id='timeToken'>"
     << "<small>NTP is still used for sync; token is stored for external time APIs.</small>"
     << "<button type='submit'>Save Time Token</button></form>"

     << "</div>" // ctApi

     << "</div>" // tConfigure

     // ════════════════════════════════════════════════════════════════════
     // CONTROL TAB
     // ════════════════════════════════════════════════════════════════════
     << "<div id='tControl' class='hidden'>"

     << "<div id='ctrlScreen'>"
     << "<section><h2>Screen Control</h2>"
     << "<div class='og' style='margin-bottom:8px;'><div class='k'>Current</div><div class='v' id='ovCurrentScreen'>-</div></div>"
     << "<form id='screenForm'><label>Switch to</label><select name='screen'>"
     << "<option value='info'>Environment Info</option>"
     << "<option value='deviceInfo'>Device Info</option>"
     << "<option value='timer'>Timer / Chronometer</option>"
     << "<option value='light'>Light Control</option>"
     << "<option value='sound'>Sound Control</option>"
     << "<option value='temperature'>Temperature Control</option>"
     << "<option value='pc'>PC Control</option>"
     << "<option value='calibrate'>Calibrate Orientation</option>"
     << "</select><button type='submit'>Switch Screen</button></form></section>"
     << "</div>"

     << "<div id='ctrlBrightness' class='hidden'>"
     << "<section><h2>Brightness</h2>"
     << "<form id='brightnessForm'><label>Brightness: <span id='brightnessValue'>100</span>%</label>"
     << "<input id='brightnessInput' name='percentage' type='range' min='0' max='100' value='100'>"
     << "<button type='submit'>Set Brightness</button></form></section>"
     << "</div>"

     << "</div>" // tControl
     << "</div>" // contentPane
     << "</div>" // portalLayout

     // ── Result / log area ─────────────────────────────────────────────────
     << "<pre id='result'></pre>"

     // ════════════════════════════════════════════════════════════════════
     // JAVASCRIPT
     // ════════════════════════════════════════════════════════════════════
     << "<script>"

     // helpers
     << "const R=document.getElementById('result');"
     << "window.addEventListener('error',e=>{if(R)R.textContent='JS error: '+e.message;});"
     << "function setText(id,v){const e=document.getElementById(id);if(e)e.textContent=(v||'-');}"
     << "function pwToggle(btn){const inp=btn.previousElementSibling;inp.type=inp.type==='password'?'text':'password';btn.textContent=inp.type==='password'?'Show':'Hide';}"
     << "function post(url,body){return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body}).then(r=>r.json()).then(j=>{if(R)R.textContent=JSON.stringify(j,null,2);return j;}).catch(err=>{if(R)R.textContent=JSON.stringify({ok:false,message:String(err)},null,2);});}"

     // tab switching (top-level)
     << "document.querySelectorAll('[data-tab]').forEach(btn=>btn.addEventListener('click',()=>{"
     << "document.querySelectorAll('[data-tab]').forEach(b=>{b.classList.remove('act');const p=document.getElementById(b.dataset.tab);if(p)p.classList.add('hidden');});"
     << "btn.classList.add('act');const activeId=btn.dataset.tab;const panel=document.getElementById(activeId);if(panel)panel.classList.remove('hidden');"
     << "const cfg=document.getElementById('cfgTabs');const ctrl=document.getElementById('ctrlTabs');if(cfg&&ctrl){cfg.classList.toggle('hidden',activeId!=='tConfigure');ctrl.classList.toggle('hidden',activeId!=='tControl');}"
     << "}));"

     // tab switching (configure subtabs)
     << "document.querySelectorAll('[data-ctab]').forEach(btn=>btn.addEventListener('click',()=>{"
     << "document.querySelectorAll('[data-ctab]').forEach(b=>{b.classList.remove('act');const p=document.getElementById(b.dataset.ctab);if(p)p.classList.add('hidden');});"
     << "btn.classList.add('act');const p=document.getElementById(btn.dataset.ctab);if(p)p.classList.remove('hidden');"
     << "}));"

     // tab switching (control subtabs)
     << "document.querySelectorAll('[data-ctrltab]').forEach(btn=>btn.addEventListener('click',()=>{"
     << "document.querySelectorAll('[data-ctrltab]').forEach(b=>{b.classList.remove('act');const p=document.getElementById(b.dataset.ctrltab);if(p)p.classList.add('hidden');});"
     << "btn.classList.add('act');const p=document.getElementById(btn.dataset.ctrltab);if(p)p.classList.remove('hidden');"
     << "}));"

     // refreshStatus — populates overview + prefills forms
     << "async function refreshStatus(){"
     << "let d;try{const r=await fetch('/api/status');d=await r.json();}catch(e){R.textContent=String(e);return;}"
     << "setText('ovDeviceName',d.device_name);setText('ovApSsid',d.ap_ssid);setText('ovApPassword',d.ap_password?'[set]':'(none)');"
     << "setText('ovApIp',d.ap_ip);setText('ovStaSsid',d.sta_ssid||d.configured_sta_ssid||'-');setText('ovStaIp',d.sta_ip||'-');"
     << "setText('ovSignal',d.sta_connected?String(d.wifi_strength_bars||0)+' / 4':'-');setText('ovCurrentScreen',d.current_screen);"
     << "const df=document.getElementById('deviceForm');if(df){df.device_suffix.value=d.device_suffix||'';const tog=df.ap_password_enabled;if(tog){tog.value=d.ap_password?'on':'off';document.getElementById('apPwField').classList.toggle('hidden',tog.value!=='on');}}"
     // apPwToggle listener moved to init block below
     << "const of=document.getElementById('otaForm');if(of){of.variant.value=d.ota_variant||'';of.manifest_url.value=d.ota_manifest_url||'';}"
     << "const o=d.ota_release||{};setText('ovCurrentVersion',o.current_version);setText('ovAvailableVersion',o.available_version);setText('ovOtaStatus',o.status_message);"
     << "const ub=document.getElementById('otaUpdateBtn');if(ub)ub.disabled=!(o.update_available===true&&!o.busy);"
     << "const sf=document.getElementById('screenForm');if(sf&&d.current_screen)sf.screen.value=d.current_screen;"
     << "const mu=document.getElementById('mqttUrl');if(mu)mu.value=d.mqtt_url||'';"
     << "const mp=document.getElementById('mqttPort');if(mp)mp.value=d.mqtt_port||8883;"
     << "const mun=document.getElementById('mqttUsername');if(mun)mun.value=d.mqtt_username||'';"
     << "const wc=document.getElementById('weatherCity');if(wc)wc.value=d.weather_city||'Istanbul';"
     << "const wco=document.getElementById('weatherCountry');if(wco)wco.value=d.weather_country||'tr';"
     << "const wt=document.getElementById('weatherToken');if(wt)wt.value=d.weather_api_token||'';"
     << "const tt=document.getElementById('timeToken');if(tt)tt.value=d.time_api_token||'';"
     << "}"

     // loadWifiSlots — fetches /api/wifi/credentials and renders slot forms
     << "async function loadWifiSlots(){"
     << "const el=document.getElementById('wifiSlots');if(!el)return;"
     << "try{"
     << "const r=await fetch('/api/wifi/credentials');const d=await r.json();"
     << "const slots=d.credentials||[];"
     << "el.innerHTML=slots.map((s,i)=>{"
     << "const ssid=(s.ssid||'').replace(/\"/g,'&quot;');const sip=(s.static_ip||'').replace(/\"/g,'&quot;');const empty=!s.ssid;"
     << "return '<div class=\"slot\"><h3>Slot '+(i+1)+(s.remember?' \u25cf ':' \u25cb ')+(empty?'(empty)':'')+'</h3>'"
     << "+'<label>SSID</label><input id=\"wSlotSsid'+i+'\" value=\"'+ssid+'\" placeholder=\"SSID\">'"
     << "+'<label>Password</label><div class=\"pw-wrap\"><input id=\"wSlotPw'+i+'\" type=\"password\" placeholder=\"(unchanged)\"><button type=\"button\" onclick=\"pwToggle(this)\">Show</button></div>'"
     << "+'<label>Static IP (optional, leave empty for DHCP)</label><input id=\"wSlotSip'+i+'\" value=\"'+sip+'\" placeholder=\"e.g. 192.168.1.50\">'"
     << "+'<div class=\"row\">'"
     << "+'<button type=\"button\" onclick=\"saveSlot('+i+')\">Save</button>'"
     << "+'<button type=\"button\" onclick=\"clearSlot('+i+')\"'+(empty?' disabled':'')+'>Clear</button>'"
     << "+'</div></div>';"
     << "}).join('');"
     << "}catch(e){el.innerHTML='<small style=\"color:#a55;\">Failed to load slots</small>'}}"

     << "async function saveSlot(i){"
     << "const ssid=document.getElementById('wSlotSsid'+i).value.trim();"
     << "const pw=document.getElementById('wSlotPw'+i).value;"
     << "const sip=document.getElementById('wSlotSip'+i).value.trim();"
     << "const p=new URLSearchParams();p.set('index',i);p.set('ssid',ssid);p.set('password',pw);p.set('remember','1');p.set('static_ip',sip);"
     << "await post('/api/wifi/credentials',p.toString());loadWifiSlots();}"

     << "async function clearSlot(i){"
     << "const p=new URLSearchParams();p.set('index',i);p.set('ssid','');p.set('password','');p.set('remember','0');p.set('static_ip','');"
     << "await post('/api/wifi/credentials',p.toString());loadWifiSlots();}"

     // loadBondedDevices
    << "async function loadBondedDevices(){"
    << "const el=document.getElementById('btBondedList');if(!el)return;"
    << "try{"
    << "const rb=await fetch('/api/bluetooth/bonded');"
    << "const d=await rb.json();"
    << "let btAvailable=false;"
    << "try{const ri=await fetch('/api/device-info');const info=await ri.json();btAvailable=info&&info.bluetooth_enabled===true;}catch(_){/* optional */}"
    << "const devices=d.devices||[];"
    << "setText('btBondCount',String(d.count!=null?d.count:devices.length));"
    << "if(devices.length===0){el.innerHTML='<small style=\"color:#666;\">(none)</small>';return;}"
    << "el.innerHTML=devices.map(dev=>{"
    << "const nm=(dev.name||'Unknown').replace(/</g,'&lt;');"
    << "const ad=(dev.address||'').replace(/</g,'&lt;');"
    << "const enc=encodeURIComponent(dev.address||'');"
    << "const connected=dev.connected===true;"
    << "const state=connected?'connected':(btAvailable?'available':'unavailable');"
    << "const statusLabel=state==='connected'?'Connected':(state==='available'?'Available':'Unavailable');"
    << "const statusCls=state==='connected'?'bt-status bt-connected':'bt-status bt-available';"
    << "return '<div class=\"bditem\"><div class=\"bdinfo\"><div class=\"bdname\">'+nm+'<span class=\"'+statusCls+'\">'+statusLabel+'</span></div><div class=\"bdaddr\">'+ad+(dev.connection_type?(' ['+dev.connection_type+']'):'')+' </div></div>'"
    << "+'<div class=\"ibtns\">'+(connected?'<button type=\"button\" class=\"danger bt-disconnect\" data-addr=\"'+enc+'\">Disconnect</button>':'')"
    << "+(!connected&&btAvailable?'<button type=\"button\" class=\"bt-connect\" data-addr=\"'+enc+'\">Connect</button>':'')"
    << "+'<button type=\"button\" class=\"danger bt-forget\" data-addr=\"'+enc+'\">Forget</button></div></div>';"
    << "}).join('');"
    << "el.querySelectorAll('.bt-forget').forEach(btn=>btn.addEventListener('click',()=>forgetDevice(decodeURIComponent(btn.dataset.addr||''))));"
    << "el.querySelectorAll('.bt-disconnect').forEach(btn=>btn.addEventListener('click',()=>disconnectBtDevice(decodeURIComponent(btn.dataset.addr||''))));"
    << "el.querySelectorAll('.bt-connect').forEach(btn=>btn.addEventListener('click',()=>connectBtDevice(decodeURIComponent(btn.dataset.addr||''))));"
    << "}catch(e){el.innerHTML='<small style=\"color:#a55;\">Failed to load bonds</small>';}}"

     << "async function forgetDevice(addr){"
     << "const p=new URLSearchParams();p.set('address',addr);"
     << "await post('/api/bluetooth/forget',p.toString());loadBondedDevices();}"

     << "async function disconnectBtDevice(addr){"
     << "const p=new URLSearchParams();p.set('address',addr);"
     << "await post('/api/bluetooth/disconnect',p.toString());setTimeout(loadBondedDevices,600);}"

    << "async function connectBtDevice(addr){"
    << "const p=new URLSearchParams();p.set('action','restart_advertising');"
    << "await post('/api/control/bluetooth',p.toString());setTimeout(loadBondedDevices,800);}"

     // loadBtStatus
     << "async function loadBtStatus(){"
     << "try{const r=await fetch('/api/device-info');const d=await r.json();"
     << "setText('btStatus',d.bluetooth_connected?'Connected':(d.bluetooth_enabled?'Enabled':'Disabled'));"
     << "setText('btBondCount',d.bluetooth_bond_count!=null?String(d.bluetooth_bond_count):'-');"
     << "const bf=document.getElementById('btForm');if(bf){bf.enabled.value=d.bluetooth_enabled===false?'off':'on';"
     << "const bn=bf.querySelector('[name=bt_name]');if(bn&&d.bluetooth_name)bn.value=d.bluetooth_name;}"
     << "}catch(e){}}"

     // WiFi scan
     << "async function doScan(){"
     << "const s=document.getElementById('scanList');s.innerHTML='<option disabled selected>Scanning...</option>';"
     << "try{const r=await fetch('/api/scan');const d=await r.json();"
     << "s.innerHTML=(d.networks||[]).map(n=>'<option value=\"'+n.ssid.replace(/\"/g,'&quot;')+'\">'+n.ssid+' ('+n.rssi+'dBm)</option>').join('')||'<option disabled selected>No networks found</option>';"
     << "R.textContent=JSON.stringify(d,null,2);"
     << "}catch(e){s.innerHTML='<option disabled selected>Scan failed</option>';R.textContent=String(e);}}"

     // Form submit wiring
     << "document.getElementById('deviceForm').addEventListener('submit',e=>{e.preventDefault();"
     << "post('/api/device',new URLSearchParams(new FormData(e.target)).toString()).then(()=>refreshStatus());});"

     << "document.getElementById('otaForm').addEventListener('submit',e=>{e.preventDefault();"
     << "post('/api/ota',new URLSearchParams(new FormData(e.target)).toString()).then(()=>refreshStatus());});"

     << "document.getElementById('otaCheckBtn').addEventListener('click',()=>post('/api/ota','action=check').then(()=>refreshStatus()));"
     << "document.getElementById('otaUpdateBtn').addEventListener('click',()=>post('/api/ota','action=update').then(()=>refreshStatus()));"

     << "document.getElementById('wifiScanBtn').addEventListener('click',doScan);"

     << "document.getElementById('wifiSaveBtn').addEventListener('click',()=>{"
     << "const ssid=(document.getElementById('wifiSsidManual').value.trim()||document.getElementById('scanList').value||'');"
     << "const pw=document.getElementById('wifiPwInput').value;"
     << "const slot=document.getElementById('wifiSlotSelect').value;"
     << "if(!ssid){R.textContent=JSON.stringify({ok:false,message:'SSID required'},null,2);return;}"
     << "const p=new URLSearchParams();p.set('index',slot);p.set('ssid',ssid);p.set('password',pw);p.set('remember','1');"
     << "post('/api/wifi/credentials',p.toString()).then(()=>loadWifiSlots());});"

     << "document.getElementById('wifiConnectBtn').addEventListener('click',()=>{"
     << "const ssid=(document.getElementById('wifiSsidManual').value.trim()||document.getElementById('scanList').value||'');"
     << "const pw=document.getElementById('wifiPwInput').value;"
     << "if(!ssid){R.textContent=JSON.stringify({ok:false,message:'SSID required'},null,2);return;}"
     << "const p=new URLSearchParams();p.set('ssid',ssid);p.set('password',pw);"
     << "post('/api/wifi',p.toString()).then(()=>refreshStatus());});"



     << "document.getElementById('btForm').addEventListener('submit',e=>{e.preventDefault();"
     << "post('/api/control/bluetooth',new URLSearchParams(new FormData(e.target)).toString()).then(()=>loadBtStatus());});"

     << "document.getElementById('btRestartBtn').addEventListener('click',()=>post('/api/control/bluetooth','action=restart_advertising'));"
     << "document.getElementById('btClearBondsBtn').addEventListener('click',()=>post('/api/control/bluetooth','action=clear_bonds').then(()=>loadBondedDevices()));"

     << "document.getElementById('btScanBtn').addEventListener('click',()=>{"
     << "const se=document.getElementById('btScanStatus');const re=document.getElementById('btScanResults');"
     << "se.textContent='Requesting scan...';re.innerHTML='';"
     << "post('/api/control/bluetooth','action=start_scan').then(r=>{"
     << "if(r&&r.ok===false){se.textContent='Scan blocked: '+(r.message||r.reason||'unknown error');return;}"
     << "se.textContent='Scanning (5s)...';"
     << "setTimeout(()=>{"
     << "fetch('/api/bluetooth/scan').then(r=>r.json()).then(d=>{"
     << "se.textContent=(d.scanning?'Still scanning...':'Done \u2014 '+d.devices.length+' device(s).')+(d.bt_ready===false?' [BT not ready]':'');"
     << "const un=d.devices.filter(v=>!v.name).length;"
     << "re.innerHTML=(d.devices.length===0?'<em style=\"color:#666\">No devices found</em>':d.devices.map(dv=>"
     << "'<div style=\"margin:3px 0;padding:4px 6px;border:1px solid #333;\"><strong>'+(dv.name||'<em style=\"color:#666\">[private]</em>')+'</strong> &nbsp;'"
     << "+'<span style=\"color:#888;\">'+dv.address+'</span> &nbsp;<span style=\"color:#9cc7ff;\">'+dv.rssi+'dBm</span></div>').join(''))"
     << "+(un>0?'<small style=\"color:#555\">[private] = BLE privacy devices (iOS/Android)</small>':'');"
     << "}).catch(()=>{se.textContent='Scan fetch failed.';});"
     << "},6000);"
     << "}).catch(()=>{se.textContent='Scan request failed.';});});"

     << "document.getElementById('mqttForm').addEventListener('submit',e=>{e.preventDefault();"
     << "post('/api/mqtt',new URLSearchParams(new FormData(e.target)).toString()).then(()=>refreshStatus());});"
     << "document.getElementById('weatherForm').addEventListener('submit',e=>{e.preventDefault();"
     << "post('/api/weather',new URLSearchParams(new FormData(e.target)).toString()).then(()=>refreshStatus());});"
     << "document.getElementById('timeForm').addEventListener('submit',e=>{e.preventDefault();"
     << "post('/api/time',new URLSearchParams(new FormData(e.target)).toString()).then(()=>refreshStatus());});"

     << "document.getElementById('screenForm').addEventListener('submit',e=>{e.preventDefault();"
     << "post('/api/control/screen',new URLSearchParams(new FormData(e.target)).toString()).then(()=>refreshStatus());});"

     << "const bIn=document.getElementById('brightnessInput');const bVal=document.getElementById('brightnessValue');"
     << "bIn.addEventListener('input',()=>bVal.textContent=bIn.value);"
     << "document.getElementById('brightnessForm').addEventListener('submit',e=>{e.preventDefault();"
     << "post('/api/control/brightness',new URLSearchParams(new FormData(e.target)).toString());});"

     // Restart / factory-reset
     << "document.getElementById('restartBtn').addEventListener('click',()=>{if(!confirm('Restart device now?'))return;post('/api/reset','');});"
     << "document.getElementById('factoryResetBtn').addEventListener('click',()=>{if(!confirm('Factory reset will erase ALL saved data and restart. Continue?'))return;post('/api/factory-reset','');});"

     // init
     << "document.getElementById('apPwToggle').addEventListener('change',function(){document.getElementById('apPwField').classList.toggle('hidden',this.value!=='on');});"
     << "refreshStatus();loadWifiSlots();loadBondedDevices();loadBtStatus();"
     << "</script></body></html>";

    return html.str();
}

std::string SetupPortal::renderDeviceInfoPage() const {
    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
         << "<title>QNOB Device Info</title><style>"
         << "body{background:#000;color:#fff;font-family:monospace;margin:0;padding:16px;}"
         << "h1{font-size:20px;margin:0 0 14px;}section{border:1px solid #fff;padding:12px;margin-bottom:12px;}"
         << ".grid{display:grid;grid-template-columns:minmax(150px,auto) 1fr;gap:8px 12px;align-items:center;}"
         << ".k{color:#9ba7b6;}.v{word-break:break-word;}"
         << "a,button{background:#000;color:#fff;border:1px solid #fff;padding:8px 10px;text-decoration:none;cursor:pointer;}"
         << "button{width:100%;margin-top:10px;}pre{white-space:pre-wrap;word-break:break-word;border:1px solid #333;padding:10px;}"
         << "</style></head><body>"
         << "<h1>QNOB Device Info</h1>"
         << "<section><div class='grid'>"
         << "<div class='k'>WiFi</div><div id='wifi' class='v'>-</div>"
         << "<div class='k'>Internet</div><div id='internet' class='v'>-</div>"
         << "<div class='k'>MQTT</div><div id='mqtt' class='v'>-</div>"
         << "<div class='k'>Bluetooth</div><div id='bluetooth' class='v'>-</div>"
         << "<div class='k'>WiFi Strength</div><div id='wifiStrength' class='v'>-</div>"
         << "<div class='k'>Battery Presence</div><div id='batteryPresence' class='v'>-</div>"
         << "<div class='k'>Battery Connected</div><div id='batteryConnected' class='v'>-</div>"
         << "<div class='k'>Battery % Available</div><div id='batteryPctAvailable' class='v'>-</div>"
         << "<div class='k'>Battery %</div><div id='batteryPct' class='v'>-</div>"
         << "<div class='k'>Battery Voltage</div><div id='batteryVoltage' class='v'>-</div>"
         << "<div class='k'>Software Configured</div><div id='swConfigured' class='v'>-</div>"
         << "<div class='k'>Software Busy</div><div id='swBusy' class='v'>-</div>"
         << "<div class='k'>Update Available</div><div id='swUpdateAvailable' class='v'>-</div>"
         << "<div class='k'>Current Version</div><div id='swCurrent' class='v'>-</div>"
         << "<div class='k'>Available Version</div><div id='swAvailable' class='v'>-</div>"
         << "<div class='k'>Status</div><div id='swStatus' class='v'>-</div>"
         << "<div class='k'>Last Update</div><div id='lastUpdate' class='v'>-</div>"
         << "</div><button id='refreshBtn' type='button'>Refresh</button></section>"
         << "<section><a href='/'>Back to setup portal</a></section>"
         << "<pre id='raw'></pre>"
         << "<script>"
         << "function yn(v){return v?'ON':'OFF';}"
         << "function txt(id,v){const e=document.getElementById(id);if(e)e.textContent=(v===undefined||v===null||v==='')?'-':String(v);}"
         << "async function refresh(){try{const r=await fetch('/api/device-info');const d=await r.json();"
         << "txt('wifi',yn(d.wifi_connected));txt('internet',yn(d.internet_connected));txt('mqtt',yn(d.mqtt_connected));"
         << "txt('bluetooth',d.bluetooth_connected?'Connected':(d.bluetooth_enabled?'Enabled':'Disabled'));"
         << "txt('wifiStrength',String(d.wifi_strength_bars||0)+' bars');txt('batteryPresence',d.battery_presence_known?'Known':'Unknown');"
         << "txt('batteryConnected',d.battery_connected?'Yes':'No');txt('batteryPctAvailable',d.battery_percentage_available?'Yes':'No');"
         << "txt('batteryPct',d.battery_percentage_available?(String(d.battery_percentage)+'%'):'N/A');"
         << "txt('batteryVoltage',d.battery_voltage>=0?(String(d.battery_voltage)+' V'):'N/A');"
         << "txt('swConfigured',d.software_configured?'Yes':'No');txt('swBusy',d.software_busy?'Yes':'No');"
         << "txt('swUpdateAvailable',d.software_update_available?'Yes':'No');txt('swCurrent',d.current_version||'-');"
         << "txt('swAvailable',d.available_version||'-');txt('swStatus',d.status_text||'-');"
         << "txt('lastUpdate',new Date().toLocaleString());document.getElementById('raw').textContent=JSON.stringify(d,null,2);}"
         << "catch(e){document.getElementById('raw').textContent=JSON.stringify({ok:false,message:String(e)},null,2);}}"
         << "document.getElementById('refreshBtn').addEventListener('click',refresh);refresh();setInterval(refresh,5000);"
         << "</script></body></html>";
    return html.str();
}

std::string SetupPortal::renderStatusJson() const {
    std::ostringstream os;
    const auto snap = ConnectivityManager::instance().getSnapshot();
    const std::string staIp      = snap.sta_ip;
    const std::string apIp       = snap.ap_ip;
    const std::string apSsid     = snap.ap_ssid;
    const std::string apPassword = snap.ap_password;
    const std::string staSsid    = snap.sta_ssid;
    std::string configuredStaSsid;
    std::string configuredStaPassword;

    if (nvsManager != nullptr) {
        int configuredIndex = nvsManager->lastConnectedNetworkIndex;
        if (configuredIndex < 0 || configuredIndex >= NUM_WIFI_CREDENTIALS ||
            nvsManager->wifiCredentials[configuredIndex].ssid.empty()) {
            configuredIndex = -1;
            for (int i = 0; i < NUM_WIFI_CREDENTIALS; ++i) {
                if (!nvsManager->wifiCredentials[i].ssid.empty()) {
                    configuredIndex = i;
                    break;
                }
            }
        }

        if (configuredIndex >= 0) {
            configuredStaSsid = nvsManager->wifiCredentials[configuredIndex].ssid;
            configuredStaPassword = nvsManager->wifiCredentials[configuredIndex].password;
        }
    }

    os << "{";
    os << "\"device_name\":\"" << jsonEscape(nvsManager ? nvsManager->deviceName : "Homio-0000") << "\",";
    os << "\"device_suffix\":\"" << jsonEscape(nvsManager ? NVSManager::extractDeviceSuffix(nvsManager->deviceName) : "0000") << "\",";
    os << "\"ap_ssid\":\"" << jsonEscape(apSsid) << "\",";
    os << "\"ap_password\":\"" << jsonEscape(apPassword) << "\",";
    os << "\"ap_ip\":\"" << jsonEscape(apIp) << "\",";
    os << "\"sta_connected\":" << (snap.wifi_state == ConnMgrState::StaConnected ? "true" : "false") << ",";
    os << "\"sta_ssid\":\"" << jsonEscape(staSsid) << "\",";
    os << "\"configured_sta_ssid\":\"" << jsonEscape(configuredStaSsid) << "\",";
    os << "\"configured_sta_password\":\"" << jsonEscape(configuredStaPassword) << "\",";
    os << "\"sta_ip\":\"" << jsonEscape(staIp) << "\",";
    os << "\"static_ip_enabled\":" << (nvsManager && nvsManager->staticIPEnabled ? "true" : "false") << ",";
    os << "\"static_ip_target_ssid\":\"" << jsonEscape(nvsManager ? nvsManager->staticIPSSID : "") << "\",";
    os << "\"current_screen\":\"" << jsonEscape(screenStatusCallback ? screenStatusCallback() : "unknown") << "\",";
    os << "\"ota_variant\":\"" << jsonEscape(nvsManager ? nvsManager->otaVariantId : "") << "\",";
    os << "\"ota_manifest_url\":\"" << jsonEscape(nvsManager ? nvsManager->otaManifestUrl : "") << "\",";
    os << "\"ota_release\":" << (otaStatusCallback ? otaStatusCallback() : "{\"configured\":false,\"busy\":false,\"update_available\":false,\"current_version\":\"unknown\",\"available_version\":\"\",\"status_message\":\"OTA unavailable\"}") << ",";
    os << "\"configured_static_ip\":\"" << jsonEscape(nvsManager ? nvsManager->staticIP : "") << "\",";
    os << "\"mqtt_url\":\"" << jsonEscape(nvsManager ? nvsManager->soundMQTTServerURL : "") << "\",";
    os << "\"mqtt_port\":" << (nvsManager ? nvsManager->soundMQTTServerPort : 8883) << ",";
    os << "\"mqtt_username\":\"" << jsonEscape(nvsManager ? nvsManager->soundMQTTUsername : "") << "\",";
    os << "\"weather_city\":\"" << jsonEscape(nvsManager ? nvsManager->weatherCity : "Istanbul") << "\",";
    os << "\"weather_country\":\"" << jsonEscape(nvsManager ? nvsManager->weatherCountryCode : "tr") << "\",";
    os << "\"weather_api_token\":\"" << jsonEscape(nvsManager ? nvsManager->weatherApiToken : "") << "\",";
    os << "\"time_api_token\":\"" << jsonEscape(nvsManager ? nvsManager->timeApiToken : "") << "\"";
    os << "}";
    return os.str();
}

std::string SetupPortal::renderDeviceInfoStatusJson() const {
    if (deviceInfoStatusCallback) {
        return deviceInfoStatusCallback();
    }

    std::ostringstream os;
    const auto snap = ConnectivityManager::instance().getSnapshot();
    os << "{";
    os << "\"wifi_connected\":" << (snap.wifi_state == ConnMgrState::StaConnected ? "true" : "false") << ",";
    os << "\"internet_connected\":false,";
    os << "\"mqtt_connected\":false,";
    os << "\"bluetooth_enabled\":false,";
    os << "\"bluetooth_connected\":false,";
    os << "\"bluetooth_hid_connected\":false,";
    os << "\"bluetooth_serial_connected\":false,";
    os << "\"wifi_strength_bars\":" << static_cast<int>(snap.rssi_bars) << ",";
    os << "\"battery_presence_known\":false,";
    os << "\"battery_connected\":false,";
    os << "\"battery_percentage_available\":false,";
    os << "\"battery_percentage\":-1,";
    os << "\"battery_voltage\":-1,";
    os << "\"software_configured\":false,";
    os << "\"software_busy\":false,";
    os << "\"software_update_available\":false,";
    os << "\"current_version\":\"unknown\",";
    os << "\"available_version\":\"\",";
    os << "\"status_text\":\"Unavailable\"";
    os << "}";
    return os.str();
}

esp_err_t SetupPortal::devicePostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->saveDeviceFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::otaPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->saveOtaFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::saveOtaFromForm(const std::string& body, std::string& responseJson) {
    if (!commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Command handler unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    commandCallback(std::string("portalOta:") + body);
    responseJson = "{\"ok\":true,\"message\":\"OTA command dispatched\"}";
    return ESP_OK;
}
esp_err_t SetupPortal::saveStaticIpFromCurrentConnection(std::string& responseJson) {
    if (!commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Command handler unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    commandCallback("portalStaticIpCurrent");
    responseJson = "{\"ok\":true,\"message\":\"Static IP command dispatched\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::saveScreenControlFromForm(const std::string& body, std::string& responseJson) {
    const std::string screen = getFormValue(body, "screen");
    if (screen.empty() || !commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Screen control unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    commandCallback(std::string("screen:") + screen);
    responseJson = std::string("{\"ok\":true,\"screen\":\"") + jsonEscape(screen) + "\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::saveBrightnessControlFromForm(const std::string& body, std::string& responseJson) {
    const std::string percent = getFormValue(body, "percentage");
    if (percent.empty() || !commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Brightness control unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    char* end = nullptr;
    const long parsed = strtol(percent.c_str(), &end, 10);
    if (end == percent.c_str() || *end != '\0') {
        responseJson = "{\"ok\":false,\"message\":\"Invalid brightness percentage\"}";
        return ESP_ERR_INVALID_ARG;
    }

    const int clamped = std::max(0, std::min(100, static_cast<int>(parsed)));
    commandCallback(std::string("setBrightness:") + std::to_string(clamped));
    responseJson = std::string("{\"ok\":true,\"percentage\":") + std::to_string(clamped) + "}";
    return ESP_OK;
}

esp_err_t SetupPortal::saveBluetoothControlFromForm(const std::string& body, std::string& responseJson) {
    if (!commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Bluetooth control unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    const std::string action = getFormValue(body, "action");
    if (action == "restart_advertising") {
        commandCallback("restartBtAdvertising");
        responseJson = "{\"ok\":true,\"action\":\"restart_advertising\"}";
        return ESP_OK;
    }
    if (action == "clear_bonds") {
        commandCallback("clearBtBonds");
        responseJson = "{\"ok\":true,\"action\":\"clear_bonds\"}";
        return ESP_OK;
    }
    if (action == "start_scan") {
        // Reject the scan request if a STA association is in progress.
        // Concurrent BLE scanning during the 4-way handshake / DHCP exchange
        // disrupts the association (T-11).
        if (ConnectivityManager::instance().getSnapshot().wifi_state ==
                ConnMgrState::StaConnecting) {
            responseJson = "{\"ok\":false,\"reason\":\"wifi_busy\","
                           "\"message\":\"WiFi association in progress — try again shortly\"}";
            return ESP_ERR_INVALID_STATE;
        }
        commandCallback("startBtScan:5");
        responseJson = "{\"ok\":true,\"action\":\"start_scan\",\"duration\":5}";
        return ESP_OK;
    }

    const std::string enabled = getFormValue(body, "enabled");
    if (enabled.empty()) {
        responseJson = "{\"ok\":false,\"message\":\"Missing enabled field\"}";
        return ESP_ERR_INVALID_ARG;
    }

    std::string value = enabled;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const bool turnOn = !(value == "off" || value == "disable" || value == "disabled" ||
                          value == "0" || value == "false");
    commandCallback(std::string("setBluetooth:") + (turnOn ? "on" : "off"));

    const std::string btName = getFormValue(body, "bt_name");
    if (!btName.empty()) {
        commandCallback(std::string("setBluetoothName:") + btName);
    }

    responseJson = std::string("{\"ok\":true,\"enabled\":") + (turnOn ? "true" : "false") + "}";
    return ESP_OK;
}

std::string SetupPortal::renderScanJson() const {
    wifi_sta_list_t staList = {};
    if (ConnectivityManager::instance().getApStaList(&staList) && staList.num > 0) {
        std::ostringstream busy;
        busy << "{\"ok\":false,\"message\":\"Scan blocked while device is connected to AP to keep captive portal stable\",\"networks\":[]}";
        return busy.str();
    }

    std::vector<wifi_ap_record_t> records;
    ConnectivityManager::instance().syncScan(records);

    std::ostringstream os;
    os << "{\"ok\":true,\"networks\":[";
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        if (i > 0) {
            os << ",";
        }
        os << "{\"ssid\":\"" << jsonEscape(reinterpret_cast<const char*>(r.ssid))
           << "\",\"rssi\":" << static_cast<int>(r.rssi) << "}";
    }
    os << "]}";
    return os.str();
}

std::string SetupPortal::urlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const char hex[3] = {value[i + 1], value[i + 2], '\0'};
            out.push_back(static_cast<char>(strtol(hex, nullptr, 16)));
            i += 2;
        } else if (value[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::string SetupPortal::jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::string SetupPortal::htmlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string SetupPortal::getFormValue(const std::string& body, const std::string& key) {
    const std::string needle = key + "=";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) {
        return "";
    }
    pos += needle.size();
    size_t end = body.find('&', pos);
    return urlDecode(body.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
}

std::string SetupPortal::readBody(httpd_req_t* req) {
    size_t len = req->content_len;
    if (len == 0 || len > MAX_POST_BODY) {
        return "";
    }

    std::string body;
    body.resize(len);
    size_t received = 0;
    while (received < len) {
        int ret = httpd_req_recv(req, &body[received], len - received);
        if (ret <= 0) {
            return "";
        }
        received += static_cast<size_t>(ret);
    }
    return body;
}

esp_err_t SetupPortal::saveDeviceFromForm(const std::string& body, std::string& responseJson) {
    if (!commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Command handler unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    commandCallback(std::string("portalDevice:") + body);
    responseJson = "{\"ok\":true,\"message\":\"Device command dispatched\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::saveWifiFromForm(const std::string& body, std::string& responseJson) {
    if (!commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Command handler unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    commandCallback(std::string("portalWifi:") + body);
    responseJson = "{\"ok\":true,\"message\":\"WiFi command dispatched\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::saveMqttFromForm(const std::string& body, std::string& responseJson) {
    if (!commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Command handler unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    commandCallback(std::string("portalMqtt:") + body);
    responseJson = "{\"ok\":true,\"message\":\"MQTT command dispatched\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::saveWeatherFromForm(const std::string& body, std::string& responseJson) {
    if (!commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Command handler unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    commandCallback(std::string("portalWeather:") + body);
    responseJson = "{\"ok\":true,\"message\":\"Weather command dispatched\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::saveTimeFromForm(const std::string& body, std::string& responseJson) {
    if (!commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Command handler unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    commandCallback(std::string("portalTime:") + body);
    responseJson = "{\"ok\":true,\"message\":\"Time command dispatched\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::wifiCredentialGetHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }
    const std::string json = self->renderWifiCredentialsJson();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t SetupPortal::wifiCredentialPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->saveWifiCredentialFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::bluetoothBondedDevicesGetHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }
    const std::string json = self->renderBluetoothBondedDevicesJson();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t SetupPortal::bluetoothBondedDeviceForgetPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    std::string response;
    self->forgetBluetoothDeviceFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::bluetoothDisconnectPostHandler(httpd_req_t* req) {
    const std::string body = readBody(req);
    const std::string address = getFormValue(body, "address");
    std::string response;
    if (address.empty()) {
        response = "{\"ok\":false,\"message\":\"Missing address\"}";
    } else if (!static_cast<SetupPortal*>(req->user_ctx)->commandCallback) {
        response = "{\"ok\":false,\"message\":\"Command handler unavailable\"}";
    } else {
        static_cast<SetupPortal*>(req->user_ctx)->commandCallback(
            "disconnectBtDevice:" + address);
        response = std::string("{\"ok\":true,\"message\":\"Disconnect requested for ") +
                   jsonEscape(address) + "\"}";
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

std::string SetupPortal::renderWifiCredentialsJson() const {
    if (!nvsManager) {
        return "{\"ok\":false,\"message\":\"NVS Manager unavailable\",\"credentials\":[]}";
    }

    std::ostringstream os;
    os << "{\"ok\":true,\"credentials\":[";

    for (int i = 0; i < NUM_WIFI_CREDENTIALS; ++i) {
        if (i > 0) os << ",";
        os << "{";
        os << "\"index\":" << i << ",";
        os << "\"ssid\":\"" << jsonEscape(nvsManager->wifiCredentials[i].ssid) << "\",";
        os << "\"password\":\"" << jsonEscape(nvsManager->wifiCredentials[i].password) << "\",";
        os << "\"static_ip\":\"" << jsonEscape(nvsManager->wifiCredentials[i].static_ip) << "\",";
        os << "\"remember\":" << (nvsManager->wifiCredentials[i].remember ? "true" : "false");
        os << "}";
    }

    os << "]}";
    return os.str();
}

std::string SetupPortal::renderBluetoothBondedDevicesJson() const {
    if (!nvsManager) {
        return "{\"ok\":false,\"message\":\"NVS Manager unavailable\",\"devices\":[]}";
    }

    std::ostringstream os;
    const std::string hidAddr = ConnectivityManager::instance().getSnapshot().bt_hid_addr;
    const std::string serAddr = ConnectivityManager::instance().getSnapshot().bt_serial_addr;
    // Normalise to upper-case for comparison
    auto toUpper = [](std::string s){ for (char& c : s) c = toupper(c); return s; };
    const std::string hidAddrUp = toUpper(hidAddr);
    const std::string serAddrUp = toUpper(serAddr);

    os << "{\"ok\":true,\"count\":" << nvsManager->bondedDeviceCount << ",\"devices\":[";

    for (int i = 0; i < nvsManager->bondedDeviceCount; ++i) {
        if (i > 0) os << ",";
        const std::string addrUp = toUpper(nvsManager->bondedDevices[i].address);
        const bool isConnected = (!hidAddrUp.empty() && hidAddrUp == addrUp) ||
                                 (!serAddrUp.empty() && serAddrUp == addrUp);
        const std::string connType = (hidAddrUp == addrUp && !hidAddrUp.empty()) ? "hid" :
                                     (serAddrUp == addrUp && !serAddrUp.empty()) ? "serial" : "";
        os << "{";
        os << "\"index\":" << i << ",";
        os << "\"address\":\"" << jsonEscape(nvsManager->bondedDevices[i].address) << "\",";
        os << "\"name\":\"" << jsonEscape(nvsManager->bondedDevices[i].name) << "\",";
        os << "\"connected\":" << (isConnected ? "true" : "false") << ",";
        os << "\"connection_type\":\"" << connType << "\"";
        os << "}";
    }

    os << "]}";
    return os.str();
}

esp_err_t SetupPortal::saveWifiCredentialFromForm(const std::string& body, std::string& responseJson) {
    if (!commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Command handler unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    commandCallback(std::string("portalWifiCredential:") + body);
    responseJson = "{\"ok\":true,\"message\":\"WiFi credential command dispatched\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::forgetBluetoothDeviceFromForm(const std::string& body, std::string& responseJson) {
    const std::string address = getFormValue(body, "address");
    if (address.empty()) {
        responseJson = "{\"ok\":false,\"message\":\"Missing Bluetooth address\"}";
        return ESP_ERR_INVALID_ARG;
    }

    if (!nvsManager) {
        responseJson = "{\"ok\":false,\"message\":\"NVS Manager unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = nvsManager->removeBondedDevice(address);
    if (ret != ESP_OK) {
        responseJson = std::string("{\"ok\":false,\"message\":\"Failed to remove device: ") + esp_err_to_name(ret) + "\"}";
        return ret;
    }

    if (commandCallback) {
        commandCallback(std::string("forgetBtDevice:") + address);
    }

    responseJson = std::string("{\"ok\":true,\"address\":\"") + jsonEscape(address) + "\",\"message\":\"Device forgotten\"}";
    return ESP_OK;
}
