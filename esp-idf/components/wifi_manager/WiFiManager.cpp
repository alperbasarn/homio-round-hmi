#include "WiFiManager.h"
#include "NVSManager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include <cstring>

static const char *TAG = "WiFiManager";

static_assert(NUM_WIFI_CREDENTIALS + 1 <= 4, "kMaxCandidates must hold every NVS slot plus the Kconfig default");

// Static instance pointer for event handler
static WiFiManager* s_wifi_manager = nullptr;

namespace {

int64_t nowMs() {
    return esp_timer_get_time() / 1000;
}

}  // namespace

WiFiManager::WiFiManager(NVSManager* nvsManager)
    : nvs_manager(nvsManager),
      initialized(false),
      wifi_connected(false),
      sta_fail_event(false),
      connect_requested(false),
      scan_done(false),
      ap_mode_active(false),
      last_reported_connected(false),
      scan_result_count(0),
      wifi_channel(WIFI_AP_CHANNEL),
      last_ap_check_time(0),
      last_scan_time(0),
      last_connect_attempt(0),
      attempt_started_ms(0),
      phase(ConnectPhase::Idle),
      candidate_count(0),
      candidate_index(0),
      active_candidate(kKconfigCandidate),
      on_connected(nullptr),
      on_disconnected(nullptr)
{
    current_ssid[0] = '\0';
    current_ip[0] = '\0';
    portMUX_INITIALIZE(&text_lock);
    for (int i = 0; i < kMaxCandidates; ++i) {
        candidates[i] = kKconfigCandidate;
    }
    s_wifi_manager = this;
}

WiFiManager::~WiFiManager()
{
    if (initialized) {
        esp_wifi_stop();
        esp_wifi_deinit();
    }
    s_wifi_manager = nullptr;
}

void WiFiManager::setText(char* dst, size_t capacity, const char* src, size_t len)
{
    if (len >= capacity) {
        len = capacity - 1;
    }
    portENTER_CRITICAL(&text_lock);
    memcpy(dst, src, len);
    dst[len] = '\0';
    portEXIT_CRITICAL(&text_lock);
}

std::string WiFiManager::readText(const char* src) const
{
    char copy[33];
    portENTER_CRITICAL(&text_lock);
    strncpy(copy, src, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    portEXIT_CRITICAL(&text_lock);
    return std::string(copy);
}

void WiFiManager::eventHandler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    (void)arg;
    if (s_wifi_manager) {
        if (event_base == WIFI_EVENT) {
            s_wifi_manager->handleWiFiEvent(event_id, event_data);
        } else if (event_base == IP_EVENT) {
            s_wifi_manager->handleIPEvent(event_id, event_data);
        }
    }
}

void WiFiManager::handleWiFiEvent(int32_t event_id, void* event_data)
{
    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started");
            break;

        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*)event_data;
            setText(current_ssid, sizeof(current_ssid), (const char*)event->ssid, event->ssid_len);
            wifi_channel = event->channel;
            ESP_LOGI(TAG, "Associated with %.*s (channel %d)", event->ssid_len, (const char*)event->ssid, wifi_channel);
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW(TAG, "Disconnected from WiFi, reason: %d", event->reason);
            wifi_connected.store(false, std::memory_order_release);
            setText(current_ip, sizeof(current_ip), "", 0);
            sta_fail_event.store(true, std::memory_order_release);
            if (on_disconnected) {
                on_disconnected();
            }
            break;
        }

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started");
            ap_mode_active = true;
            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "AP stopped");
            ap_mode_active = false;
            break;

        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
            ESP_LOGI(TAG, "Station " MACSTR " joined AP", MAC2STR(event->mac));
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
            ESP_LOGI(TAG, "Station " MACSTR " left AP", MAC2STR(event->mac));
            break;
        }

        case WIFI_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "WiFi scan completed");
            scan_done.store(true, std::memory_order_release);
            break;

        default:
            break;
    }
}

void WiFiManager::handleIPEvent(int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        setText(current_ip, sizeof(current_ip), ip_str, strlen(ip_str));

        ESP_LOGI(TAG, "Got IP address: %s", ip_str);
        wifi_connected.store(true, std::memory_order_release);

        if (on_connected) {
            on_connected(readText(current_ssid), std::string(ip_str));
        }
    }
}

esp_err_t WiFiManager::initialize()
{
    ESP_LOGI(TAG, "Initializing WiFi manager...");

#ifdef CONFIG_ENABLE_WIFI_STATION
    // When Matter is active, the networking stack (esp_netif, event loop,
    // WiFi driver) is already initialized by esp_matter::start().
    // We only register our event handlers on the existing event loop.
    ESP_LOGI(TAG, "Matter mode: skipping networking stack init (managed by Matter)");
#else
    // Non-Matter build: initialize the full networking stack ourselves
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());
#endif

    // Register event handlers (works on both Matter-managed and standalone stacks)
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &eventHandler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &eventHandler, this, nullptr));

    initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized");

    return ESP_OK;
}

std::string WiFiManager::generateAPName()
{
    std::string ap_name = WIFI_AP_SSID_PREFIX;
    if (nvs_manager && !nvs_manager->deviceName.empty()) {
        ap_name = nvs_manager->deviceName;
    } else {
        // Add random suffix
        ap_name += std::to_string(esp_random() % 1000);
    }
    return ap_name;
}

esp_err_t WiFiManager::startAPMode()
{
    std::string ap_name = generateAPName();

    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, ap_name.c_str(), sizeof(ap_config.ap.ssid) - 1);
    strncpy((char*)ap_config.ap.password, WIFI_AP_PASSWORD, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.ssid_len = ap_name.length();
    ap_config.ap.channel = wifi_channel;
    ap_config.ap.max_connection = WIFI_AP_MAX_CONNECTIONS;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    if (strlen(WIFI_AP_PASSWORD) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    ESP_LOGI(TAG, "AP started: %s (channel %d)", ap_name.c_str(), wifi_channel);
    ap_mode_active = true;

    return ESP_OK;
}

esp_err_t WiFiManager::startAPSTAMode()
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    return startAPMode();
}

void WiFiManager::requestConnect()
{
    connect_requested.store(true, std::memory_order_release);
}

esp_err_t WiFiManager::connectToWiFi()
{
    if (!nvs_manager) {
        ESP_LOGE(TAG, "NVS manager not set");
        return ESP_ERR_INVALID_STATE;
    }
    requestConnect();
    return ESP_OK;
}

void WiFiManager::beginSequence(int64_t now)
{
    candidate_count = 0;
    bool hasStoredNetworks = false;
    const int lastIndex = nvs_manager ? nvs_manager->lastConnectedNetworkIndex : -1;

    // First the last connected network, then the other stored slots, then the Kconfig default
    // when nothing is stored at all: the same order the blocking sequence used.
    if (nvs_manager && lastIndex >= 0 && lastIndex < NUM_WIFI_CREDENTIALS &&
        !nvs_manager->wifiCredentials[lastIndex].ssid.empty()) {
        candidates[candidate_count++] = lastIndex;
        hasStoredNetworks = true;
    }
    for (int i = 0; nvs_manager && i < NUM_WIFI_CREDENTIALS; i++) {
        if (i == lastIndex) continue;
        if (nvs_manager->wifiCredentials[i].ssid.empty()) continue;
        candidates[candidate_count++] = i;
        hasStoredNetworks = true;
    }
    if (!hasStoredNetworks && strlen(CONFIG_QNOB_WIFI_SSID) > 0) {
        candidates[candidate_count++] = kKconfigCandidate;
    }

    last_connect_attempt = now;
    if (candidate_count == 0) {
        ESP_LOGW(TAG, "No stored network and no Kconfig default: AP only");
        finishSequence(false, now);
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    candidate_index = 0;
    if (!startCandidate(candidate_index, now)) {
        finishSequence(false, now);
    }
}

bool WiFiManager::startCandidate(int position, int64_t now)
{
    if (position < 0 || position >= candidate_count) {
        return false;
    }
    const int slot = candidates[position];
    wifi_config_t sta_config = {};
    if (slot == kKconfigCandidate) {
        ESP_LOGI(TAG, "Trying default WiFi from Kconfig: %s", CONFIG_QNOB_WIFI_SSID);
        strncpy((char*)sta_config.sta.ssid, CONFIG_QNOB_WIFI_SSID, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char*)sta_config.sta.password, CONFIG_QNOB_WIFI_PASSWORD, sizeof(sta_config.sta.password) - 1);
    } else {
        const WifiCredential& cred = nvs_manager->wifiCredentials[slot];
        ESP_LOGI(TAG, "Connecting to: %s (slot %d, %d of %d)", cred.ssid.c_str(), slot, position + 1, candidate_count);
        strncpy((char*)sta_config.sta.ssid, cred.ssid.c_str(), sizeof(sta_config.sta.ssid) - 1);
        strncpy((char*)sta_config.sta.password, cred.password.c_str(), sizeof(sta_config.sta.password) - 1);
    }
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    sta_fail_event.store(false, std::memory_order_release);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return false;
    }
    active_candidate = slot;
    attempt_started_ms = now;
    phase = ConnectPhase::Trying;
    return true;
}

void WiFiManager::serviceSequence(int64_t now)
{
    if (wifi_connected.load(std::memory_order_acquire)) {
        if (active_candidate == kKconfigCandidate) {
            ESP_LOGI(TAG, "Connected to default WiFi SSID; storing it in slot 0");
            nvs_manager->wifiCredentials[0].ssid = CONFIG_QNOB_WIFI_SSID;
            nvs_manager->wifiCredentials[0].password = CONFIG_QNOB_WIFI_PASSWORD;
            nvs_manager->wifiCredentials[0].remember = true;
            nvs_manager->lastConnectedNetworkIndex = 0;
        } else {
            nvs_manager->lastConnectedNetworkIndex = active_candidate;
        }
        nvs_manager->saveWiFiCredentials();
        finishSequence(true, now);
        return;
    }

    const bool failed = sta_fail_event.load(std::memory_order_acquire);
    const bool timedOut = now - attempt_started_ms > WIFI_CONNECT_TIMEOUT_MS;
    if (!failed && !timedOut) {
        return;
    }

    ESP_LOGW(TAG, "Candidate %d of %d %s", candidate_index + 1, candidate_count, failed ? "failed" : "timed out");
    esp_wifi_disconnect();
    candidate_index++;
    if (!startCandidate(candidate_index, now)) {
        finishSequence(false, now);
    }
}

void WiFiManager::finishSequence(bool connected, int64_t now)
{
    phase = ConnectPhase::Done;
    last_connect_attempt = now;
    if (connected) {
        ESP_LOGI(TAG, "Connected to %s", readText(current_ssid).c_str());
        return;
    }
    // No successful STA connection: keep AP only to avoid repeated STA warning spam.
    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_LOGW(TAG, "Failed to connect to any stored network");
}

void WiFiManager::disconnect()
{
    esp_wifi_disconnect();
    wifi_connected.store(false, std::memory_order_release);
}

esp_err_t WiFiManager::scanNetworks()
{
    wifi_scan_config_t scan_config = {
        .ssid = nullptr,
        .bssid = nullptr,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = { .min = 100, .max = 300 },
            .passive = 0
        }
    };

    scan_done.store(false, std::memory_order_release);
    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);  // Non-blocking
    if (ret == ESP_OK) {
        last_scan_time = nowMs();
    }
    return ret;
}

int WiFiManager::getSignalStrength()
{
    int8_t rssi = getRSSI();
    if (rssi >= -50) return 4;
    if (rssi >= -60) return 3;
    if (rssi >= -70) return 2;
    if (rssi >= -80) return 1;
    return 0;
}

int8_t WiFiManager::getRSSI() const
{
    if (!isConnected()) return -100;

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return -100;
}

std::string WiFiManager::getIPAddress() const
{
    return readText(current_ip);
}

std::string WiFiManager::getSSID() const
{
    return readText(current_ssid);
}

void WiFiManager::update()
{
    if (!initialized) return;

    const int64_t current_time = nowMs();
    const bool connected = isConnected();

    if (connected && !last_reported_connected) {
        ESP_LOGI(TAG, "WiFi link up: %s %s", readText(current_ssid).c_str(), readText(current_ip).c_str());
    } else if (!connected && last_reported_connected) {
        ESP_LOGW(TAG, "WiFi link down");
    }
    last_reported_connected = connected;

    if (connect_requested.exchange(false, std::memory_order_acq_rel) && phase != ConnectPhase::Trying) {
        beginSequence(current_time);
    }

    if (phase == ConnectPhase::Trying) {
        serviceSequence(current_time);
    } else if (!connected && current_time - last_connect_attempt > WIFI_RETRY_INTERVAL_MS) {
        ESP_LOGI(TAG, "Attempting to reconnect to WiFi...");
        beginSequence(current_time);
    }

    // Check AP status
    if (current_time - last_ap_check_time > WIFI_AP_CHECK_INTERVAL_MS) {
        last_ap_check_time = current_time;

        wifi_mode_t mode;
        esp_wifi_get_mode(&mode);

        if (mode == WIFI_MODE_APSTA || mode == WIFI_MODE_AP) {
            // AP should be active
            esp_netif_ip_info_t ip_info;
            esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            if (ap_netif && esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
                if (ip_info.ip.addr == 0) {
                    ESP_LOGW(TAG, "AP has no IP, restarting...");
                    startAPMode();
                }
            }
        }
    }
}

// C-compatible interface
extern "C" {

static char g_ip_buffer[16];

wifi_manager_handle_t wifi_manager_create(void* nvs_manager)
{
    return new WiFiManager(static_cast<NVSManager*>(nvs_manager));
}

void wifi_manager_destroy(wifi_manager_handle_t handle)
{
    delete static_cast<WiFiManager*>(handle);
}

esp_err_t wifi_manager_init(wifi_manager_handle_t handle)
{
    return static_cast<WiFiManager*>(handle)->initialize();
}

esp_err_t wifi_manager_connect(wifi_manager_handle_t handle)
{
    return static_cast<WiFiManager*>(handle)->connectToWiFi();
}

bool wifi_manager_is_connected(wifi_manager_handle_t handle)
{
    return static_cast<WiFiManager*>(handle)->isConnected();
}

const char* wifi_manager_get_ip(wifi_manager_handle_t handle)
{
    const std::string ip = static_cast<WiFiManager*>(handle)->getIPAddress();
    strncpy(g_ip_buffer, ip.c_str(), sizeof(g_ip_buffer) - 1);
    g_ip_buffer[sizeof(g_ip_buffer) - 1] = '\0';
    return g_ip_buffer;
}

}
