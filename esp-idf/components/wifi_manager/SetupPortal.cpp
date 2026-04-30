#include "SetupPortal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "WiFiManager.h"
#include "NVSManager.h"

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

esp_err_t SetupPortal::startHttpServer() {
    if (httpServer != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    config.ctrl_port = HTTP_PORT + 1;
    config.max_uri_handlers = 30;
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

    const std::string apIp = wifiManager != nullptr ? wifiManager->getAPIPAddress() : "192.168.4.1";
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
    const std::string html = self->renderRootPage();
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

esp_err_t SetupPortal::captiveRedirectHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    const std::string apIp = (self->wifiManager != nullptr) ? self->wifiManager->getAPIPAddress() : "192.168.4.1";
    const std::string location = "http://" + (apIp.empty() ? std::string("192.168.4.1") : apIp) + "/";

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
     << "body{background:#000;color:#fff;font-family:monospace;margin:0;padding:16px;}"
     << "h1{font-size:20px;margin:0 0 16px;}h2{font-size:15px;margin:20px 0 8px;}"
     << "form,section{border:1px solid #fff;padding:12px;margin-bottom:12px;}"
     << "label{display:block;margin:8px 0 4px;}input,select,button{width:100%;background:#000;color:#fff;border:1px solid #fff;padding:8px;box-sizing:border-box;}"
     << "button{cursor:pointer;}small{display:block;margin-top:6px;color:#bbb;}pre{white-space:pre-wrap;word-break:break-word;}"
     << ".overview{display:grid;grid-template-columns:minmax(110px,auto) 1fr auto;gap:8px 10px;align-items:center;}"
     << ".overview .label{color:#bbb;}.overview .value{word-break:break-word;}.toggle,.action-inline{width:auto;padding:4px 8px;}"
     << ".tabs,.subtabs{display:flex;gap:8px;margin-bottom:12px;}.tabs button,.subtabs button{width:auto;padding:8px 12px;}"
     << ".active-tab{background:#fff;color:#000;}.hidden{display:none;}"
     << "</style></head><body>"
     << "<h1>QNOB Setup Portal</h1>"
     << "<div class='tabs'><button id='configureTabBtn' class='active-tab' type='button' data-tab='configureTab'>Configure</button><button id='controlTabBtn' type='button' data-tab='controlTab'>Control</button></div>"
     << "<div id='configureTab'>"
     << "<section><h2>Network Info</h2><div id='status' class='overview'>"
     << "<div class='label'>Device</div><div class='value' id='ovDeviceName'>-</div><div></div>"
     << "<div class='label'>AP SSID</div><div class='value' id='ovApSsid'>-</div><div></div>"
     << "<div class='label'>AP Password</div><div class='value' id='ovApPassword'>-</div><button class='toggle' type='button' data-target='ovApPassword'>Show</button>"
     << "<div class='label'>AP IP</div><div class='value' id='ovApIp'>-</div><div></div>"
     << "<div class='label'>Configured WiFi</div><div class='value' id='ovConfiguredStaSsid'>-</div><div></div>"
     << "<div class='label'>Configured Password</div><div class='value' id='ovConfiguredStaPassword'>-</div><button class='toggle' type='button' data-target='ovConfiguredStaPassword'>Show</button>"
     << "<div class='label'>Connected WiFi</div><div class='value' id='ovStaSsid'>-</div><div></div>"
     << "<div class='label'>STA IP</div><div class='value' id='ovStaIp'>-</div><button id='staticIpBtn' class='action-inline hidden' type='button'>Set Static IP</button>"
     << "<div class='label'>Static IP Target</div><div class='value' id='ovStaticIpTarget'>-</div><div></div>"
    << "<div class='label'>Software Version</div><div class='value' id='ovCurrentVersion'>-</div><div></div>"
    << "<div class='label'>Available Version</div><div class='value' id='ovAvailableVersion'>-</div><div></div>"
    << "<div class='label'>OTA Status</div><div class='value' id='ovOtaStatus'>-</div><div></div>"
     << "</div></section>"
     << "<form id='deviceForm'><h2>Device</h2><label>Device Suffix</label><input name='device_suffix' maxlength='4' placeholder='0000'>"
     << "<small>Device name is always Homio-&lt;suffix&gt; using only A-Z and 0-9.</small>"
     << "<label>Access Point Password</label><input name='ap_password' type='password' placeholder='Homio-0000'>"
     << "<small>Leave blank to reset the password to the device name.</small>"
     << "<button type='submit'>Save Device Settings</button></form>"
        << "<form id='otaForm'><h2>OTA</h2><label>Variant</label><input name='variant' placeholder='esp32s3_lcd128'>"
        << "<small>This variant selects which release image your device is allowed to install.</small>"
        << "<label>Manifest URL (optional)</label><input name='manifest_url' placeholder='https://.../latest/{variant}.json'>"
        << "<small>Use {variant} placeholder or leave empty to use default server path.</small>"
        << "<button type='submit'>Save OTA Settings</button><button id='otaCheckBtn' type='button'>Check for Update</button><button id='otaUpdateBtn' type='button' disabled>Update Now</button></form>"
    << "<form id='wifiForm'><h2>WiFi</h2><label>Scan Results</label><select id='scanList'><option value='' selected disabled>Press Scan WiFi to search</option></select>"
     << "<label>Or SSID</label><input name='ssid_manual' placeholder='SSID'>"
     << "<label>Password</label><input name='password' type='password' placeholder='Password'>"
     << "<button type='button' onclick='scan()'>Scan WiFi</button><button type='submit'>Save & Connect</button></form>"
     << "<form id='mqttForm'><h2>MQTT</h2><label>Broker URL/IP</label><input name='url'>"
     << "<label>Port</label><input name='port' value='8883'>"
     << "<label>Username</label><input name='username'><label>Password</label><input type='password' name='password'>"
     << "<small>Saved to both sound and light MQTT configs.</small><button type='submit'>Save MQTT</button></form>"
     << "<form id='weatherForm'><h2>Weather</h2><label>API Token</label><input name='api_token'>"
     << "<label>City</label><input name='city' value='Istanbul'><label>Country Code</label><input name='country' value='tr'>"
     << "<button type='submit'>Save Weather</button></form>"
     << "<form id='timeForm'><h2>Time</h2><label>Time API Token</label><input name='api_token'>"
     << "<small>NTP is still used for sync; token is stored for external time APIs.</small>"
     << "<button type='submit'>Save Time Token</button></form>"
    << "<section><h2>Device Info</h2><a href='/device-info' style='color:#9cc7ff;'>Open device info page</a></section>"
    << "<section><h2>Device Control</h2><form method='POST' action='/api/reset'><button id='resetBtn' type='submit' style='background:#600;border-color:#f44;color:#fff;'>Restart Device</button></form><small><a href='/api/reset' style='color:#9cc7ff;'>Direct reset link</a></small></section>"
     << "</div>"
     << "<div id='controlTab' class='hidden'>"
     << "<div class='subtabs'><button class='active-tab' type='button'>Screens</button></div>"
         << "<section><h2>Screen Control</h2><div class='overview'><div class='label'>Current Screen</div><div class='value' id='ovCurrentScreen'>-</div><div></div></div><form id='screenForm'><label>Screen</label><select name='screen'>"
    << "<option value='info'>Environment Info</option><option value='deviceInfo'>Device Info</option><option value='timer'>Timer / Chronometer</option><option value='light'>Light Control</option><option value='sound'>Sound Control</option><option value='temperature'>Temperature Control</option><option value='pc'>PC Control</option><option value='calibrate'>Calibrate Orientation</option>"
     << "</select><button type='submit'>Switch Screen</button></form></section>"
     << "<section><h2>Screen Brightness</h2><form id='brightnessForm'><label>Brightness: <span id='brightnessValue'>100</span>%</label><input id='brightnessInput' name='percentage' type='range' min='0' max='100' value='100'><button type='submit'>Set Brightness</button></form></section>"
     << "<section><h2>Bluetooth</h2><form id='bluetoothForm'><label>Status: <span id='bluetoothStatus'>-</span></label><select name='enabled'><option value='on'>Enabled</option><option value='off'>Disabled</option></select><label style='display:block;margin-top:10px;'>Name<input name='bt_name' type='text' maxlength='32' placeholder='Qnob PC Control' style='margin-left:8px;width:220px;'></label><small>Name change takes effect after device restart.</small><button type='submit'>Apply Bluetooth</button></form>"
     << "<div style='margin-top:12px;border-top:1px solid #444;padding-top:10px;'>"
     << "<div style='margin-bottom:8px;'>Bonded devices: <strong id='btBondCount'>-</strong></div>"
     << "<button type='button' id='btRestartBtn' style='margin-right:8px;'>Restart Advertising</button>"
     << "<button type='button' id='btClearBondsBtn' style='background:#400;border-color:#f44;color:#fff;margin-right:8px;'>Clear All Bonds</button>"
     << "<button type='button' id='btScanBtn'>Scan BLE Devices</button>"
     << "<small style='display:block;margin-top:6px;'>Clear bonds if the device does not appear on iPhone/PC, then scan for it again in Bluetooth settings.</small>"
     << "<div id='btScanStatus' style='margin-top:8px;color:#9cc7ff;'></div>"
     << "<div id='btScanResults' style='margin-top:6px;'></div>"
     << "</div></section></div>"
     << "<pre id='result'></pre>"
     << "<script>"
     << "const resultEl=document.getElementById('result');const secrets={};const staticIpBtn=document.getElementById('staticIpBtn');"
     << "function maskValue(v){if(!v)return '(not set)';return '*'.repeat(Math.max(8,Math.min(v.length,16)));}"
     << "function setText(id,v){const el=document.getElementById(id);if(el)el.textContent=v||'-';}"
     << "function setSecret(id,v){secrets[id]=v||'';setText(id,maskValue(v));}"
     << "function toggleSecret(btn){const id=btn.dataset.target;const showing=btn.dataset.showing==='true';setText(id,showing?maskValue(secrets[id]):(secrets[id]||'(not set)'));btn.dataset.showing=showing?'false':'true';btn.textContent=showing?'Show':'Hide';}"
     << "document.querySelectorAll('.toggle').forEach(b=>b.addEventListener('click',()=>toggleSecret(b)));"
     << "document.querySelectorAll('[data-tab]').forEach(btn=>btn.addEventListener('click',()=>{document.querySelectorAll('[data-tab]').forEach(b=>b.classList.remove('active-tab'));btn.classList.add('active-tab');document.getElementById('configureTab').classList.toggle('hidden',btn.dataset.tab!=='configureTab');document.getElementById('controlTab').classList.toggle('hidden',btn.dataset.tab!=='controlTab');}));"
        << "async function refreshStatus(){const r=await fetch('/api/status');const d=await r.json();setText('ovDeviceName',d.device_name||'-');setText('ovApSsid',d.ap_ssid||'-');setSecret('ovApPassword',d.ap_password||'');setText('ovApIp',d.ap_ip||'-');setText('ovConfiguredStaSsid',d.configured_sta_ssid||'(not set)');setSecret('ovConfiguredStaPassword',d.configured_sta_password||'');setText('ovStaSsid',d.sta_ssid||((d.sta_connected&&d.configured_sta_ssid)||'-'));setText('ovStaIp',d.sta_ip||'-');setText('ovStaticIpTarget',d.static_ip_target_ssid||'(not set)');setText('ovCurrentScreen',d.current_screen||'-');const sf=document.getElementById('screenForm');if(sf&&d.current_screen){sf.screen.value=d.current_screen;}staticIpBtn.classList.toggle('hidden',!(d.sta_connected&&d.sta_ip));const df=document.getElementById('deviceForm');if(df){df.device_suffix.value=d.device_suffix||'0000';df.ap_password.placeholder=d.ap_password||'Homio-0000';}const of=document.getElementById('otaForm');if(of){of.variant.value=d.ota_variant||'';of.manifest_url.value=d.ota_manifest_url||'';}const o=d.ota_release||{};setText('ovCurrentVersion',o.current_version||'-');setText('ovAvailableVersion',o.available_version||'-');setText('ovOtaStatus',o.status_message||'-');const ub=document.getElementById('otaUpdateBtn');if(ub){ub.disabled=!(o.update_available===true&&o.busy!==true);}fetch('/api/device-info').then(r=>r.json()).then(di=>{const bf=document.getElementById('bluetoothForm');if(bf){bf.enabled.value=di.bluetooth_enabled===false?'off':'on';const bnf=bf.querySelector('[name=bt_name]');if(bnf&&di.bluetooth_name)bnf.value=di.bluetooth_name;}setText('bluetoothStatus',di.bluetooth_connected?'Connected':(di.bluetooth_enabled?'Enabled':'Disabled'));setText('btBondCount',di.bluetooth_bond_count!=null?String(di.bluetooth_bond_count):'-');}).catch(()=>{});}"
     << "async function scan(){const s=document.getElementById('scanList');s.innerHTML='';const wait=document.createElement('option');wait.textContent='Scanning...';wait.disabled=true;wait.selected=true;s.appendChild(wait);try{const r=await fetch('/api/scan');const d=await r.json();s.innerHTML='';(d.networks||[]).forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+'dBm)';s.appendChild(o);});if(!s.options.length){const o=document.createElement('option');o.textContent='No networks found';o.disabled=true;o.selected=true;s.appendChild(o);}resultEl.textContent=JSON.stringify(d,null,2);}catch(e){s.innerHTML='';const o=document.createElement('option');o.textContent='Scan failed';o.disabled=true;o.selected=true;s.appendChild(o);resultEl.textContent=JSON.stringify({ok:false,message:String(e)},null,2);}}"
     << "function formBody(f){return new URLSearchParams(new FormData(f)).toString();}"
     << "async function postForm(id,url,extra){const f=document.getElementById(id);const data=formBody(f)+(extra||'');const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:data});resultEl.textContent=JSON.stringify(await r.json(),null,2);refreshStatus();}"
     << "document.getElementById('deviceForm').addEventListener('submit',e=>{e.preventDefault();postForm('deviceForm','/api/device');});"
        << "document.getElementById('otaForm').addEventListener('submit',e=>{e.preventDefault();postForm('otaForm','/api/ota');});"
      << "document.getElementById('otaCheckBtn').addEventListener('click',()=>{fetch('/api/ota',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'action=check'}).then(r=>r.json()).then(j=>{resultEl.textContent=JSON.stringify(j,null,2);refreshStatus();}).catch(err=>{resultEl.textContent=JSON.stringify({ok:false,message:String(err)},null,2);});});"
      << "document.getElementById('otaUpdateBtn').addEventListener('click',()=>{fetch('/api/ota',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'action=update'}).then(r=>r.json()).then(j=>{resultEl.textContent=JSON.stringify(j,null,2);refreshStatus();}).catch(err=>{resultEl.textContent=JSON.stringify({ok:false,message:String(err)},null,2);});});"
     << "document.getElementById('wifiForm').addEventListener('submit',e=>{e.preventDefault();const f=e.target;const selected=document.getElementById('scanList').value;const manual=f.ssid_manual.value.trim();const ssid=manual||selected;if(!ssid||ssid==='No networks found'||ssid==='Scan failed'||ssid==='Scanning...'){resultEl.textContent=JSON.stringify({ok:false,message:'SSID is required'},null,2);return;}const p=new URLSearchParams();p.set('ssid',ssid);p.set('password',f.password.value||'');fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()}).then(r=>r.json()).then(j=>{resultEl.textContent=JSON.stringify(j,null,2);refreshStatus();}).catch(err=>{resultEl.textContent=JSON.stringify({ok:false,message:String(err)},null,2);});});"
     << "document.getElementById('screenForm').addEventListener('submit',e=>{e.preventDefault();postForm('screenForm','/api/control/screen');});"
     << "const brightnessInput=document.getElementById('brightnessInput');const brightnessValue=document.getElementById('brightnessValue');brightnessInput.addEventListener('input',()=>{brightnessValue.textContent=brightnessInput.value;});document.getElementById('brightnessForm').addEventListener('submit',e=>{e.preventDefault();postForm('brightnessForm','/api/control/brightness');});"
     << "document.getElementById('bluetoothForm').addEventListener('submit',e=>{e.preventDefault();postForm('bluetoothForm','/api/control/bluetooth');});"
     << "document.getElementById('btRestartBtn').addEventListener('click',()=>{fetch('/api/control/bluetooth',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'action=restart_advertising'}).then(r=>r.json()).then(j=>{resultEl.textContent=JSON.stringify(j,null,2);refreshStatus();}).catch(err=>{resultEl.textContent=JSON.stringify({ok:false,message:String(err)},null,2);});});"
     << "document.getElementById('btClearBondsBtn').addEventListener('click',()=>{fetch('/api/control/bluetooth',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'action=clear_bonds'}).then(r=>r.json()).then(j=>{resultEl.textContent=JSON.stringify(j,null,2);refreshStatus();}).catch(err=>{resultEl.textContent=JSON.stringify({ok:false,message:String(err)},null,2);});});"
     << "document.getElementById('btScanBtn').addEventListener('click',()=>{"
     << "const statusEl=document.getElementById('btScanStatus');const resultsEl=document.getElementById('btScanResults');"
     << "statusEl.textContent='Scanning for 5 seconds...';resultsEl.innerHTML='';"
     << "fetch('/api/control/bluetooth',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'action=start_scan'}).catch(()=>{});"
     << "setTimeout(()=>{"
     << "fetch('/api/bluetooth/scan').then(r=>r.json()).then(d=>{"
     << "statusEl.textContent=(d.scanning?'Still scanning...':'Scan done ['+(d.scan_status||'')+'] — '+(d.devices.length)+' device(s).')+(d.bt_ready===false?' ⚠ BT not ready':'');"
     << "const unnamedCount=d.devices.filter(v=>!v.name).length;"
     << "resultsEl.innerHTML=(d.devices.length===0?'<em style=\"color:#888\">No devices found</em>':d.devices.map(dev=>"
     << "'<div style=\"margin:4px 0;padding:4px 6px;border:1px solid #444;\"><strong>'+(dev.name||'<em style=\"color:#888\">[private]</em>')+'</strong> &nbsp; <span style=\"color:#aaa\">'+dev.address+'</span> &nbsp; <span style=\"color:#9cc7ff;\">'+dev.rssi+'dBm</span></div>').join(''))"
     << "+(unnamedCount>0?'<small style=\"color:#666\">[private] = iOS/Android/Windows devices with BLE privacy (rotating address, no name broadcast)</small>':'');"
     << "}).catch(()=>{statusEl.textContent='Scan failed.';});},6000);"
     << "});"
     << "staticIpBtn.addEventListener('click',()=>{fetch('/api/static-ip/current',{method:'POST'}).then(r=>r.json()).then(j=>{resultEl.textContent=JSON.stringify(j,null,2);refreshStatus();}).catch(err=>{resultEl.textContent=JSON.stringify({ok:false,message:String(err)},null,2);});});"
     << "document.getElementById('mqttForm').addEventListener('submit',e=>{e.preventDefault();postForm('mqttForm','/api/mqtt');});"
     << "document.getElementById('weatherForm').addEventListener('submit',e=>{e.preventDefault();postForm('weatherForm','/api/weather');});"
     << "document.getElementById('timeForm').addEventListener('submit',e=>{e.preventDefault();postForm('timeForm','/api/time');});"
    << "refreshStatus();"
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
    const std::string staIp = wifiManager ? wifiManager->getIPAddress() : "";
    const std::string apIp = wifiManager ? wifiManager->getAPIPAddress() : "";
    const std::string apSsid = wifiManager ? wifiManager->getAPSSID() : "";
    const std::string apPassword = wifiManager ? wifiManager->getAPPassword() : "";
    const std::string staSsid = wifiManager ? wifiManager->getSSID() : "";
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
    os << "\"sta_connected\":" << (wifiManager && wifiManager->isConnected() ? "true" : "false") << ",";
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
    os << "\"configured_static_ip\":\"" << jsonEscape(nvsManager ? nvsManager->staticIP : "") << "\"";
    os << "}";
    return os.str();
}

std::string SetupPortal::renderDeviceInfoStatusJson() const {
    if (deviceInfoStatusCallback) {
        return deviceInfoStatusCallback();
    }

    std::ostringstream os;
    os << "{";
    os << "\"wifi_connected\":" << (wifiManager && wifiManager->isConnected() ? "true" : "false") << ",";
    os << "\"internet_connected\":false,";
    os << "\"mqtt_connected\":false,";
    os << "\"bluetooth_enabled\":false,";
    os << "\"bluetooth_connected\":false,";
    os << "\"bluetooth_hid_connected\":false,";
    os << "\"bluetooth_serial_connected\":false,";
    os << "\"wifi_strength_bars\":" << (wifiManager ? wifiManager->getSignalStrength() : 0) << ",";
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
    if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK && staList.num > 0) {
        std::ostringstream busy;
        busy << "{\"ok\":false,\"message\":\"Scan blocked while device is connected to AP to keep captive portal stable\",\"networks\":[]}";
        return busy.str();
    }

    esp_wifi_set_mode(WIFI_MODE_APSTA);

    wifi_scan_config_t scanConfig = {};
    scanConfig.ssid = nullptr;
    scanConfig.bssid = nullptr;
    scanConfig.channel = 0;
    scanConfig.show_hidden = true;
    scanConfig.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scanConfig.scan_time.active.min = 100;
    scanConfig.scan_time.active.max = 300;
    scanConfig.scan_time.passive = 0;

    std::vector<wifi_ap_record_t> records;
    if (esp_wifi_scan_start(&scanConfig, true) == ESP_OK) {
        uint16_t count = 20;
        records.resize(count);
        if (esp_wifi_scan_get_ap_records(&count, records.data()) == ESP_OK) {
            records.resize(count);
        } else {
            records.clear();
        }
    }

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
