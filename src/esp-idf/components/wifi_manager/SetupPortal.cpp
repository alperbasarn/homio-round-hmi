#include "SetupPortal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

// Embedded portal assets — resolved by the linker from .rodata.
// CMakeLists EMBED_TXTFILES appends a NUL byte; _end points past it.
extern const char portal_index_html_start[]       asm("_binary_index_html_start");
extern const char portal_index_html_end[]         asm("_binary_index_html_end");
extern const char portal_device_info_html_start[] asm("_binary_device_info_html_start");
extern const char portal_device_info_html_end[]   asm("_binary_device_info_html_end");

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

    httpd_uri_t wifiEnabledPost = {};
    wifiEnabledPost.uri = "/api/wifi/enabled";
    wifiEnabledPost.method = HTTP_POST;
    wifiEnabledPost.handler = wifiEnabledPostHandler;
    wifiEnabledPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &wifiEnabledPost);

    httpd_uri_t portalEnabledPost = {};
    portalEnabledPost.uri = "/api/portal/enabled";
    portalEnabledPost.method = HTTP_POST;
    portalEnabledPost.handler = portalEnabledPostHandler;
    portalEnabledPost.user_ctx = this;
    httpd_register_uri_handler(httpServer, &portalEnabledPost);

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
    const size_t len = static_cast<size_t>(portal_index_html_end - portal_index_html_start - 1);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, portal_index_html_start, static_cast<ssize_t>(len));
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
    const size_t len = static_cast<size_t>(portal_device_info_html_end - portal_device_info_html_start - 1);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, portal_device_info_html_start, static_cast<ssize_t>(len));
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
    os << "\"time_api_token\":\"" << jsonEscape(nvsManager ? nvsManager->timeApiToken : "") << "\",";
    os << "\"wifi_sta_enabled\":" << (nvsManager && !nvsManager->wifiStaEnabled ? "false" : "true") << ",";
    os << "\"wifi_ap_enabled\":"  << (nvsManager && !nvsManager->wifiApEnabled  ? "false" : "true") << ",";
    os << "\"portal_enabled\":"   << (nvsManager && !nvsManager->portalEnabled  ? "false" : "true") << ",";
    os << "\"bluetooth_enabled\":" << (nvsManager && !nvsManager->bluetoothEnabled ? "false" : "true");
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

// ── T-18: WiFi STA / AP enable-disable ──────────────────────────────────────

esp_err_t SetupPortal::wifiEnabledPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) return ESP_FAIL;
    std::string response;
    self->saveWifiEnabledFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::saveWifiEnabledFromForm(const std::string& body, std::string& responseJson) {
    if (nvsManager == nullptr) {
        responseJson = "{\"ok\":false,\"message\":\"NVS unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    const std::string staVal = getFormValue(body, "wifi_sta_enabled");
    const std::string apVal  = getFormValue(body, "wifi_ap_enabled");

    const bool staEnabled = !(staVal == "off" || staVal == "0" || staVal == "false");
    const bool apEnabled  = !(apVal  == "off" || apVal  == "0" || apVal  == "false");

    // Safety rail: disabling AP is only allowed when STA is connected (T-18).
    if (!apEnabled) {
        const auto snap = ConnectivityManager::instance().getSnapshot();
        if (snap.wifi_state != ConnMgrState::StaConnected) {
            responseJson = "{\"ok\":false,\"reason\":\"sta_not_connected\","
                           "\"message\":\"Disable AP only while connected to home WiFi\"}";
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (!staVal.empty()) nvsManager->wifiStaEnabled = staEnabled;
    if (!apVal.empty())  nvsManager->wifiApEnabled  = apEnabled;
    nvsManager->saveEnabledFlags();

    // Apply AP change immediately (start or stop AP).
    if (!apVal.empty()) {
        if (apEnabled) {
            if (wifiManager != nullptr) wifiManager->startAPMode();
        }
        // Stopping the AP requires esp_wifi_set_mode(STA-only) which is handled
        // by ConnMgr; a reboot is the safest path for now.
    }

    responseJson = std::string("{\"ok\":true,\"wifi_sta_enabled\":") +
                   (nvsManager->wifiStaEnabled ? "true" : "false") + ",\"wifi_ap_enabled\":" +
                   (nvsManager->wifiApEnabled  ? "true" : "false") + "}";
    return ESP_OK;
}

// ── T-19: Captive portal enable-disable ─────────────────────────────────────

esp_err_t SetupPortal::portalEnabledPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) return ESP_FAIL;
    std::string response;
    self->savePortalEnabledFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
}

esp_err_t SetupPortal::savePortalEnabledFromForm(const std::string& body, std::string& responseJson) {
    if (nvsManager == nullptr) {
        responseJson = "{\"ok\":false,\"message\":\"NVS unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    const std::string val = getFormValue(body, "portal_enabled");
    const bool enable = !(val == "off" || val == "0" || val == "false");

    // Safety: disabling the portal without STA connectivity locks the user out (T-19).
    if (!enable) {
        const auto snap = ConnectivityManager::instance().getSnapshot();
        if (snap.wifi_state != ConnMgrState::StaConnected) {
            responseJson = "{\"ok\":false,\"reason\":\"no_remote_management\","
                           "\"message\":\"Connect to home WiFi before disabling the portal\"}";
            return ESP_ERR_INVALID_STATE;
        }
    }

    nvsManager->portalEnabled = enable;
    nvsManager->saveEnabledFlags();

    if (!enable) {
        // Tell the caller the portal is going down *after* this response,
        // then stop it via commandCallback so the response can be sent first.
        if (commandCallback) {
            commandCallback("stopPortal");
        }
    }

    const char* warning = enable ? "" : " Portal stops after this response.";
    responseJson = std::string("{\"ok\":true,\"portal_enabled\":") +
                   (enable ? "true" : "false") + ",\"message\":\"Saved." + warning + "\"}";
    return ESP_OK;
}
