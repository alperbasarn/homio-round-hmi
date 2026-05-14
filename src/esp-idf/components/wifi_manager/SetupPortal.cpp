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

void SetupPortal::setCommandCallback(std::function<void(const std::string&)> callback) {
    commandCallback = std::move(callback);
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
    config.max_uri_handlers = 40;
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

    size_t nameEnd = 12;
    while (nameEnd < reqLen && request[nameEnd] != 0) {
        nameEnd += static_cast<size_t>(request[nameEnd]) + 1;
    }
    if (nameEnd + 4 >= reqLen) {
        return false;
    }

    const uint16_t qtype = static_cast<uint16_t>((request[nameEnd + 1] << 8) | request[nameEnd + 2]);
    const size_t questionEnd = nameEnd + 5;

    uint8_t response[512] = {0};

    response[0] = request[0];
    response[1] = request[1];
    response[2] = 0x81;
    response[3] = 0x80;
    response[4] = request[4];
    response[5] = request[5];
    if (questionEnd > 12 && questionEnd <= sizeof(response)) {
        memcpy(response + 12, request + 12, questionEnd - 12);
    }

    if (qtype != 1 /* A */) {
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

    response[6] = 0x00;
    response[7] = 0x01;

    if (questionEnd + 16 > sizeof(response)) {
        return false;
    }

    size_t pos = questionEnd;
    response[pos++] = 0xC0; response[pos++] = 0x0C;
    response[pos++] = 0x00; response[pos++] = 0x01;
    response[pos++] = 0x00; response[pos++] = 0x01;
    response[pos++] = 0x00; response[pos++] = 0x00;
    response[pos++] = 0x00; response[pos++] = 0x3C;
    response[pos++] = 0x00; response[pos++] = 0x04;

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

// ─── HTTP handlers ────────────────────────────────────────────────────────────

esp_err_t SetupPortal::rootGetHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) return ESP_FAIL;
    const std::string html = self->renderRootPage();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html.c_str(), static_cast<ssize_t>(html.size()));
}

esp_err_t SetupPortal::statusGetHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) return ESP_FAIL;
    const std::string json = self->renderStatusJson();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t SetupPortal::wifiCredentialGetHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) return ESP_FAIL;
    const std::string json = self->renderWifiCredentialsJson();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t SetupPortal::wifiCredentialPostHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) return ESP_FAIL;
    std::string response;
    self->saveWifiCredentialFromForm(readBody(req), response);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), static_cast<ssize_t>(response.size()));
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
    if (self == nullptr) return ESP_FAIL;

    const auto snap = ConnectivityManager::instance().getSnapshot();
    const std::string apIp = snap.ap_ip[0] ? std::string(snap.ap_ip) : std::string("192.168.4.1");
    const std::string location = "http://" + apIp + "/";

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Location", location.c_str());
    return httpd_resp_send(req, nullptr, 0);
}

// ── T-18: WiFi STA/AP enable-disable ─────────────────────────────────────────

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

    if (!apVal.empty() && apEnabled) {
        if (wifiManager != nullptr) wifiManager->startAPMode();
    }

    responseJson = std::string("{\"ok\":true,\"wifi_sta_enabled\":") +
                   (nvsManager->wifiStaEnabled ? "true" : "false") + ",\"wifi_ap_enabled\":" +
                   (nvsManager->wifiApEnabled  ? "true" : "false") + "}";
    return ESP_OK;
}

// ── T-19: Captive portal enable-disable ──────────────────────────────────────

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

    if (!enable && commandCallback) {
        commandCallback("stopPortal");
    }

    const char* warning = enable ? "" : " Portal stops after this response.";
    responseJson = std::string("{\"ok\":true,\"portal_enabled\":") +
                   (enable ? "true" : "false") + ",\"message\":\"Saved." + warning + "\"}";
    return ESP_OK;
}

// ─── Page rendering ───────────────────────────────────────────────────────────

std::string SetupPortal::renderRootPage() const {
    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
         << "<title>QNOB WiFi Setup</title><style>"
         << "body{background:#000;color:#fff;font-family:monospace;margin:0;padding:16px;max-width:520px;}"
         << "h1{font-size:18px;margin:0 0 12px;}h2{font-size:13px;margin:12px 0 6px;border-bottom:1px solid #333;padding-bottom:3px;}"
         << "section,form{border:1px solid #333;padding:10px;margin-bottom:10px;}"
         << "label{display:block;margin:6px 0 2px;color:#bbb;}"
         << "input{width:100%;background:#111;color:#fff;border:1px solid #555;padding:6px;box-sizing:border-box;margin-bottom:2px;}"
         << "button{width:100%;background:#000;color:#fff;border:1px solid #fff;padding:8px;cursor:pointer;margin-top:6px;}"
         << "button:hover{background:#222;}.danger{border-color:#a33!important;color:#f88!important;}"
         << ".og{display:grid;grid-template-columns:minmax(90px,auto) 1fr;gap:4px 10px;align-items:start;}"
         << ".k{color:#666;}.v{word-break:break-all;}"
         << ".slot{border:1px solid #333;padding:8px;margin-bottom:8px;}"
         << ".slot h3{margin:0 0 6px;font-size:11px;color:#777;}"
         << ".row{display:flex;gap:6px;}.row button{width:auto;flex:1;margin-top:0;}"
         << ".pw-wrap{display:flex;gap:4px;margin-bottom:2px;}.pw-wrap input{flex:1;margin:0;}"
         << ".pw-wrap button{width:auto;padding:5px 8px;margin-top:0;flex-shrink:0;font-size:11px;}"
         << ".notice{background:#111;border:1px solid #336;padding:10px;margin-bottom:10px;color:#9cc7ff;font-size:12px;}"
         << "pre{white-space:pre-wrap;word-break:break-word;border:1px solid #333;padding:8px;margin-top:10px;font-size:11px;}"
         << "</style></head><body>"
         << "<h1>QNOB WiFi Setup</h1>"

         << "<div class='notice'>&#x1F4F1; Configure MQTT, Bluetooth, OTA, display, and other settings from the <strong>Qnob Companion</strong> app once connected to your network.</div>"

         << "<section><h2>Status</h2><div class='og'>"
         << "<div class='k'>Device</div><div class='v' id='ovDeviceName'>-</div>"
         << "<div class='k'>AP IP</div><div class='v' id='ovApIp'>-</div>"
         << "<div class='k'>Network</div><div class='v' id='ovStaSsid'>-</div>"
         << "<div class='k'>Device IP</div><div class='v' id='ovStaIp'>-</div>"
         << "</div></section>"

         << "<section><h2>Saved Networks</h2><div id='wifiSlots'><small style='color:#666;'>Loading...</small></div></section>"

         << "<section><h2>Factory Reset</h2>"
         << "<small style='color:#888;'>Wipes all saved data (WiFi, settings, pairing). Use only to recover a misconfigured device.</small>"
         << "<button id='factoryResetBtn' type='button' class='danger' style='margin-top:8px;'>Factory Reset (Wipe All &amp; Restart)</button>"
         << "</section>"

         << "<pre id='result'></pre>"

         << "<script>"
         << "function pwToggle(btn){const i=btn.previousElementSibling;i.type=i.type==='password'?'text':'password';btn.textContent=i.type==='password'?'Show':'Hide';}"
         << "function setText(id,v){const e=document.getElementById(id);if(e)e.textContent=v||'-';}"
         << "const R=document.getElementById('result');"
         << "async function post(url,body){try{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const j=await r.json();R.textContent=JSON.stringify(j,null,2);return j;}catch(e){R.textContent=String(e);}}"

         << "async function refreshStatus(){"
         << "try{const r=await fetch('/api/status');const d=await r.json();"
         << "setText('ovDeviceName',d.device_name);setText('ovApIp',d.ap_ip);"
         << "setText('ovStaSsid',d.sta_connected?d.sta_ssid:'(not connected)');"
         << "setText('ovStaIp',d.sta_ip||'-');}"
         << "catch(e){R.textContent=String(e);}}"

         << "async function loadWifiSlots(){"
         << "const el=document.getElementById('wifiSlots');if(!el)return;"
         << "try{const r=await fetch('/api/wifi/credentials');const d=await r.json();"
         << "const slots=d.credentials||[];"
         << "el.innerHTML=slots.map((s,i)=>{"
         << "const ssid=(s.ssid||'').replace(/\"/g,'&quot;');const empty=!s.ssid;"
         << "return '<div class=\"slot\"><h3>Slot '+(i+1)+(s.remember?' ● ':' ○ ')+(empty?'(empty)':'')+'</h3>'"
         << "+'<label>SSID</label><input id=\"wS'+i+'\" value=\"'+ssid+'\" placeholder=\"Network name\">'"
         << "+'<label>Password</label><div class=\"pw-wrap\"><input id=\"wP'+i+'\" type=\"password\" placeholder=\"(unchanged)\"><button type=\"button\" onclick=\"pwToggle(this)\">Show</button></div>'"
         << "+'<div class=\"row\" style=\"margin-top:6px;\">'"
         << "+'<button type=\"button\" onclick=\"saveSlot('+i+')\">Save &amp; Connect</button>'"
         << "+'<button type=\"button\" onclick=\"clearSlot('+i+')\"'+(empty?' disabled':'')+'>Clear</button>'"
         << "+'</div></div>';"
         << "}).join('');"
         << "}catch(e){el.innerHTML='<small style=\"color:#a55;\">Failed to load slots</small>';}}"

         << "async function saveSlot(i){"
         << "const ssid=document.getElementById('wS'+i).value.trim();"
         << "const pw=document.getElementById('wP'+i).value;"
         << "const p=new URLSearchParams();p.set('index',i);p.set('ssid',ssid);p.set('password',pw);p.set('remember','1');"
         << "await post('/api/wifi/credentials',p.toString());loadWifiSlots();setTimeout(refreshStatus,2000);}"

         << "async function clearSlot(i){"
         << "const p=new URLSearchParams();p.set('index',i);p.set('ssid','');p.set('password','');p.set('remember','0');"
         << "await post('/api/wifi/credentials',p.toString());loadWifiSlots();}"

         << "document.getElementById('factoryResetBtn').addEventListener('click',()=>{"
         << "if(!confirm('Factory reset will erase ALL saved data and restart. Continue?'))return;"
         << "post('/api/factory-reset','');});"

         << "refreshStatus();loadWifiSlots();"
         << "</script></body></html>";

    return html.str();
}

std::string SetupPortal::renderStatusJson() const {
    std::ostringstream os;
    const auto snap = ConnectivityManager::instance().getSnapshot();

    std::string configuredStaSsid;
    if (nvsManager != nullptr) {
        int idx = nvsManager->lastConnectedNetworkIndex;
        if (idx < 0 || idx >= NUM_WIFI_CREDENTIALS || nvsManager->wifiCredentials[idx].ssid.empty()) {
            idx = -1;
            for (int i = 0; i < NUM_WIFI_CREDENTIALS; ++i) {
                if (!nvsManager->wifiCredentials[i].ssid.empty()) { idx = i; break; }
            }
        }
        if (idx >= 0) configuredStaSsid = nvsManager->wifiCredentials[idx].ssid;
    }

    os << "{";
    os << "\"device_name\":\"" << jsonEscape(nvsManager ? nvsManager->deviceName : "Homio-0000") << "\",";
    os << "\"device_suffix\":\"" << jsonEscape(nvsManager ? NVSManager::extractDeviceSuffix(nvsManager->deviceName) : "0000") << "\",";
    os << "\"ap_ssid\":\"" << jsonEscape(snap.ap_ssid) << "\",";
    os << "\"ap_password\":\"" << jsonEscape(snap.ap_password) << "\",";
    os << "\"ap_ip\":\"" << jsonEscape(snap.ap_ip) << "\",";
    os << "\"sta_connected\":" << (snap.wifi_state == ConnMgrState::StaConnected ? "true" : "false") << ",";
    os << "\"sta_ssid\":\"" << jsonEscape(snap.sta_ssid) << "\",";
    os << "\"configured_sta_ssid\":\"" << jsonEscape(configuredStaSsid) << "\",";
    os << "\"sta_ip\":\"" << jsonEscape(snap.sta_ip) << "\",";
    os << "\"static_ip_enabled\":" << (nvsManager && nvsManager->staticIPEnabled ? "true" : "false") << ",";
    os << "\"wifi_strength_bars\":" << static_cast<int>(snap.rssi_bars) << ",";
    os << "\"wifi_sta_enabled\":" << (nvsManager ? (nvsManager->wifiStaEnabled ? "true" : "false") : "true") << ",";
    os << "\"wifi_ap_enabled\":"  << (nvsManager ? (nvsManager->wifiApEnabled  ? "true" : "false") : "true") << ",";
    os << "\"portal_enabled\":"   << (nvsManager && !nvsManager->portalEnabled ? "false" : "true");
    os << "}";
    return os.str();
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
        os << "\"static_ip\":\"" << jsonEscape(nvsManager->wifiCredentials[i].static_ip) << "\",";
        os << "\"remember\":" << (nvsManager->wifiCredentials[i].remember ? "true" : "false");
        os << "}";
    }

    os << "]}";
    return os.str();
}

// ─── Form saves ───────────────────────────────────────────────────────────────

esp_err_t SetupPortal::saveWifiCredentialFromForm(const std::string& body, std::string& responseJson) {
    if (!commandCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Command handler unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    commandCallback(std::string("portalWifiCredential:") + body);
    responseJson = "{\"ok\":true,\"message\":\"WiFi credential saved\"}";
    return ESP_OK;
}

// ─── Utilities ────────────────────────────────────────────────────────────────

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
        if (c == '\\' || c == '"') out.push_back('\\');
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
    if (pos == std::string::npos) return "";
    pos += needle.size();
    size_t end = body.find('&', pos);
    return urlDecode(body.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
}

std::string SetupPortal::readBody(httpd_req_t* req) {
    size_t len = req->content_len;
    if (len == 0 || len > MAX_POST_BODY) return "";

    std::string body;
    body.resize(len);
    size_t received = 0;
    while (received < len) {
        int ret = httpd_req_recv(req, &body[received], len - received);
        if (ret <= 0) return "";
        received += static_cast<size_t>(ret);
    }
    return body;
}
