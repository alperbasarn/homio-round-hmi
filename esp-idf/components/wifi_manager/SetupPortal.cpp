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
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
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
    config.max_uri_handlers = 20;
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

    const char* captiveUris[] = {
        "/generate_204",
        "/hotspot-detect.html",
        "/library/test/success.html",
        "/success.txt",
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
    }

    httpd_uri_t catchAll = {};
    catchAll.uri = "/*";
    catchAll.method = HTTP_GET;
    catchAll.handler = captiveRedirectHandler;
    catchAll.user_ctx = this;
    httpd_register_uri_handler(httpServer, &catchAll);

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

    size_t questionEnd = 12;
    while (questionEnd < reqLen && request[questionEnd] != 0) {
        questionEnd += static_cast<size_t>(request[questionEnd]) + 1;
    }
    if (questionEnd + 5 >= reqLen) {
        return false;
    }
    questionEnd += 5;

    uint8_t response[512] = {0};
    if (questionEnd + 16 > sizeof(response)) {
        return false;
    }

    response[0] = request[0];
    response[1] = request[1];
    response[2] = 0x81;
    response[3] = 0x80;
    response[4] = request[4];
    response[5] = request[5];
    response[6] = 0x00;
    response[7] = 0x01;

    memcpy(response + 12, request + 12, questionEnd - 12);
    size_t pos = questionEnd;

    response[pos++] = 0xC0;
    response[pos++] = 0x0C;
    response[pos++] = 0x00;
    response[pos++] = 0x01;
    response[pos++] = 0x00;
    response[pos++] = 0x01;
    response[pos++] = 0x00;
    response[pos++] = 0x00;
    response[pos++] = 0x00;
    response[pos++] = 0x3C;
    response[pos++] = 0x00;
    response[pos++] = 0x04;

    const std::string apIp = wifiManager != nullptr ? wifiManager->getAPIPAddress() : "192.168.4.1";
    ip4_addr_t ip = {};
    if (!ip4addr_aton(apIp.c_str(), &ip)) {
        ip4addr_aton("192.168.4.1", &ip);
    }
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

esp_err_t SetupPortal::captiveRedirectHandler(httpd_req_t* req) {
    auto* self = static_cast<SetupPortal*>(req->user_ctx);
    if (self == nullptr) {
        return ESP_FAIL;
    }

    const std::string html = self->renderRootPage();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    return httpd_resp_send(req, html.c_str(), static_cast<ssize_t>(html.size()));
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
     << "</div></section>"
     << "<form id='deviceForm'><h2>Device</h2><label>Device Suffix</label><input name='device_suffix' maxlength='4' placeholder='0000'>"
     << "<small>Device name is always Homio-&lt;suffix&gt; using only A-Z and 0-9.</small>"
     << "<label>Access Point Password</label><input name='ap_password' type='password' placeholder='Homio-0000'>"
     << "<small>Leave blank to reset the password to the device name.</small>"
     << "<button type='submit'>Save Device Settings</button></form>"
     << "<form id='wifiForm'><h2>WiFi</h2><label>Scan Results</label><select id='scanList'></select>"
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
     << "</div>"
     << "<div id='controlTab' class='hidden'>"
     << "<div class='subtabs'><button class='active-tab' type='button'>Screens</button></div>"
         << "<section><h2>Screen Control</h2><div class='overview'><div class='label'>Current Screen</div><div class='value' id='ovCurrentScreen'>-</div><div></div></div><form id='screenForm'><label>Screen</label><select name='screen'>"
     << "<option value='info'>Info Screen</option><option value='deviceInfo'>Device Info Screen</option><option value='home'>Home</option><option value='sound'>Sound</option><option value='light'>Light</option><option value='calibrate'>Calibrate Orientation</option>"
     << "</select><button type='submit'>Switch Screen</button></form></section></div>"
     << "<pre id='result'></pre>"
     << "<script>"
     << "const resultEl=document.getElementById('result');const secrets={};const staticIpBtn=document.getElementById('staticIpBtn');"
     << "function maskValue(v){if(!v)return '(not set)';return '*'.repeat(Math.max(8,Math.min(v.length,16)));}"
     << "function setText(id,v){const el=document.getElementById(id);if(el)el.textContent=v||'-';}"
     << "function setSecret(id,v){secrets[id]=v||'';setText(id,maskValue(v));}"
     << "function toggleSecret(btn){const id=btn.dataset.target;const showing=btn.dataset.showing==='true';setText(id,showing?maskValue(secrets[id]):(secrets[id]||'(not set)'));btn.dataset.showing=showing?'false':'true';btn.textContent=showing?'Show':'Hide';}"
     << "document.querySelectorAll('.toggle').forEach(b=>b.addEventListener('click',()=>toggleSecret(b)));"
     << "document.querySelectorAll('[data-tab]').forEach(btn=>btn.addEventListener('click',()=>{document.querySelectorAll('[data-tab]').forEach(b=>b.classList.remove('active-tab'));btn.classList.add('active-tab');document.getElementById('configureTab').classList.toggle('hidden',btn.dataset.tab!=='configureTab');document.getElementById('controlTab').classList.toggle('hidden',btn.dataset.tab!=='controlTab');}));"
         << "async function refreshStatus(){const r=await fetch('/api/status');const d=await r.json();setText('ovDeviceName',d.device_name||'-');setText('ovApSsid',d.ap_ssid||'-');setSecret('ovApPassword',d.ap_password||'');setText('ovApIp',d.ap_ip||'-');setText('ovConfiguredStaSsid',d.configured_sta_ssid||'(not set)');setSecret('ovConfiguredStaPassword',d.configured_sta_password||'');setText('ovStaSsid',d.sta_ssid||((d.sta_connected&&d.configured_sta_ssid)||'-'));setText('ovStaIp',d.sta_ip||'-');setText('ovStaticIpTarget',d.static_ip_target_ssid||'(not set)');setText('ovCurrentScreen',d.current_screen||'-');const sf=document.getElementById('screenForm');if(sf&&d.current_screen){sf.screen.value=d.current_screen;}staticIpBtn.classList.toggle('hidden',!(d.sta_connected&&d.sta_ip));const df=document.getElementById('deviceForm');if(df){df.device_suffix.value=d.device_suffix||'0000';df.ap_password.placeholder=d.ap_password||'Homio-0000';}}"
     << "async function scan(){const s=document.getElementById('scanList');s.innerHTML='';const wait=document.createElement('option');wait.textContent='Scanning...';wait.disabled=true;wait.selected=true;s.appendChild(wait);try{const r=await fetch('/api/scan');const d=await r.json();s.innerHTML='';(d.networks||[]).forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+'dBm)';s.appendChild(o);});if(!s.options.length){const o=document.createElement('option');o.textContent='No networks found';o.disabled=true;o.selected=true;s.appendChild(o);}resultEl.textContent=JSON.stringify(d,null,2);}catch(e){s.innerHTML='';const o=document.createElement('option');o.textContent='Scan failed';o.disabled=true;o.selected=true;s.appendChild(o);resultEl.textContent=JSON.stringify({ok:false,message:String(e)},null,2);}}"
     << "function formBody(f){return new URLSearchParams(new FormData(f)).toString();}"
     << "async function postForm(id,url,extra){const f=document.getElementById(id);const data=formBody(f)+(extra||'');const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:data});resultEl.textContent=JSON.stringify(await r.json(),null,2);refreshStatus();}"
     << "document.getElementById('deviceForm').addEventListener('submit',e=>{e.preventDefault();postForm('deviceForm','/api/device');});"
     << "document.getElementById('wifiForm').addEventListener('submit',e=>{e.preventDefault();const f=e.target;const selected=document.getElementById('scanList').value;const manual=f.ssid_manual.value.trim();const ssid=manual||selected;if(!ssid||ssid==='No networks found'||ssid==='Scan failed'||ssid==='Scanning...'){resultEl.textContent=JSON.stringify({ok:false,message:'SSID is required'},null,2);return;}const p=new URLSearchParams();p.set('ssid',ssid);p.set('password',f.password.value||'');fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()}).then(r=>r.json()).then(j=>{resultEl.textContent=JSON.stringify(j,null,2);refreshStatus();}).catch(err=>{resultEl.textContent=JSON.stringify({ok:false,message:String(err)},null,2);});});"
     << "document.getElementById('screenForm').addEventListener('submit',e=>{e.preventDefault();postForm('screenForm','/api/control/screen');});"
     << "staticIpBtn.addEventListener('click',()=>{fetch('/api/static-ip/current',{method:'POST'}).then(r=>r.json()).then(j=>{resultEl.textContent=JSON.stringify(j,null,2);refreshStatus();}).catch(err=>{resultEl.textContent=JSON.stringify({ok:false,message:String(err)},null,2);});});"
     << "document.getElementById('mqttForm').addEventListener('submit',e=>{e.preventDefault();postForm('mqttForm','/api/mqtt');});"
     << "document.getElementById('weatherForm').addEventListener('submit',e=>{e.preventDefault();postForm('weatherForm','/api/weather');});"
     << "document.getElementById('timeForm').addEventListener('submit',e=>{e.preventDefault();postForm('timeForm','/api/time');});"
     << "refreshStatus();scan();"
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
    os << "\"configured_static_ip\":\"" << jsonEscape(nvsManager ? nvsManager->staticIP : "") << "\"";
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

esp_err_t SetupPortal::saveStaticIpFromCurrentConnection(std::string& responseJson) {
    if (wifiManager == nullptr || nvsManager == nullptr) {
        responseJson = "{\"ok\":false,\"message\":\"Static IP unavailable\"}";
        return ESP_FAIL;
    }

    const esp_err_t err = wifiManager->saveCurrentConnectionAsStaticIP();
    if (err != ESP_OK) {
        responseJson = std::string("{\"ok\":false,\"message\":\"Failed to save static IP: ") + esp_err_to_name(err) + "\"}";
        return err;
    }

    responseJson = std::string("{\"ok\":true,\"ssid\":\"") + jsonEscape(nvsManager->staticIPSSID) +
                   "\",\"ip\":\"" + jsonEscape(nvsManager->staticIP) + "\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::saveScreenControlFromForm(const std::string& body, std::string& responseJson) {
    const std::string screen = getFormValue(body, "screen");
    if (screen.empty() || !screenControlCallback) {
        responseJson = "{\"ok\":false,\"message\":\"Screen control unavailable\"}";
        return ESP_ERR_INVALID_STATE;
    }

    if (!screenControlCallback(screen)) {
        responseJson = "{\"ok\":false,\"message\":\"Unknown screen\"}";
        return ESP_ERR_INVALID_ARG;
    }

    responseJson = std::string("{\"ok\":true,\"screen\":\"") + jsonEscape(screen) + "\"}";
    return ESP_OK;
}

std::string SetupPortal::renderScanJson() const {
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
    os << "{\"networks\":[";
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
    if (nvsManager == nullptr || wifiManager == nullptr) {
        responseJson = "{\"ok\":false,\"message\":\"Device settings unavailable\"}";
        return ESP_FAIL;
    }

    const std::string suffix = getFormValue(body, "device_suffix");
    const std::string password = getFormValue(body, "ap_password");

    nvsManager->saveDeviceName(suffix);
    if (!password.empty()) {
        const esp_err_t passwordErr = nvsManager->saveAccessPointPassword(password);
        if (passwordErr != ESP_OK) {
            responseJson = "{\"ok\":false,\"message\":\"Password must be 8-63 characters\"}";
            return passwordErr;
        }
    }

    const esp_err_t wifiErr = wifiManager->syncAccessPointConfig();
    if (wifiErr != ESP_OK) {
        responseJson = std::string("{\"ok\":false,\"message\":\"AP refresh failed: ") + esp_err_to_name(wifiErr) + "\"}";
        return wifiErr;
    }

    responseJson = std::string("{\"ok\":true,\"device_name\":\"") +
                   jsonEscape(nvsManager->deviceName) +
                   "\",\"ap_password\":\"" +
                   jsonEscape(nvsManager->accessPointPassword) +
                   "\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::saveWifiFromForm(const std::string& body, std::string& responseJson) {
    const std::string ssid = getFormValue(body, "ssid");
    const std::string password = getFormValue(body, "password");

    if (ssid.empty() || wifiManager == nullptr) {
        responseJson = "{\"ok\":false,\"message\":\"SSID is required\"}";
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t err = wifiManager->connectToNetwork(ssid, password, true);
    if (err == ESP_OK) {
        responseJson = "{\"ok\":true,\"message\":\"Connected and saved\"}";
    } else {
        responseJson = std::string("{\"ok\":false,\"message\":\"Connect failed: ") + esp_err_to_name(err) + "\"}";
    }
    return err;
}

esp_err_t SetupPortal::saveMqttFromForm(const std::string& body, std::string& responseJson) {
    if (nvsManager == nullptr) {
        responseJson = "{\"ok\":false,\"message\":\"NVS unavailable\"}";
        return ESP_FAIL;
    }

    const std::string url = getFormValue(body, "url");
    const std::string portStr = getFormValue(body, "port");
    const std::string username = getFormValue(body, "username");
    const std::string password = getFormValue(body, "password");

    int port = 8883;
    if (!portStr.empty()) {
        port = std::max(1, std::min(65535, atoi(portStr.c_str())));
    }

    nvsManager->saveSoundMQTTServer(url, port, username, password);
    nvsManager->saveLightMQTTServer(url, port, username, password);

    responseJson = "{\"ok\":true,\"message\":\"MQTT saved\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::saveWeatherFromForm(const std::string& body, std::string& responseJson) {
    if (nvsManager == nullptr) {
        responseJson = "{\"ok\":false,\"message\":\"NVS unavailable\"}";
        return ESP_FAIL;
    }

    const std::string token = getFormValue(body, "api_token");
    const std::string city = getFormValue(body, "city");
    const std::string country = getFormValue(body, "country");

    nvsManager->saveWeatherConfig(token, city.empty() ? "Istanbul" : city, country.empty() ? "tr" : country);
    responseJson = "{\"ok\":true,\"message\":\"Weather settings saved\"}";
    return ESP_OK;
}

esp_err_t SetupPortal::saveTimeFromForm(const std::string& body, std::string& responseJson) {
    if (nvsManager == nullptr) {
        responseJson = "{\"ok\":false,\"message\":\"NVS unavailable\"}";
        return ESP_FAIL;
    }

    const std::string token = getFormValue(body, "api_token");
    nvsManager->saveTimeApiToken(token);
    responseJson = "{\"ok\":true,\"message\":\"Time token saved\"}";
    return ESP_OK;
}
