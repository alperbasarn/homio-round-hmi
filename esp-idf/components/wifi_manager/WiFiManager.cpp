#include "WiFiManager.h"
#include "NVSManager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "sdkconfig.h"
#include "lwip/dns.h"
#include "lwip/sockets.h"
#include <cstring>

static const char *TAG = "WiFiManager";

// Static instance pointer for event handler
static WiFiManager* s_wifi_manager = nullptr;

WiFiManager::WiFiManager(NVSManager* nvsManager)
    : nvs_manager(nvsManager),
      wifi_event_group(nullptr),
      initialized(false),
      wifi_connected(false),
      ap_mode_active(false),
      internet_available(false),
      scan_result_count(0),
      wifi_channel(WIFI_AP_CHANNEL),
      last_ap_check_time(0),
      last_scan_time(0),
      last_connect_attempt(0),
      on_connected(nullptr),
      on_disconnected(nullptr)
{
    s_wifi_manager = this;
}

WiFiManager::~WiFiManager()
{
    if (initialized) {
        esp_wifi_stop();
        esp_wifi_deinit();
    }
    if (wifi_event_group) {
        vEventGroupDelete(wifi_event_group);
    }
    s_wifi_manager = nullptr;
}

void WiFiManager::eventHandler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
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
            current_ssid = std::string((char*)event->ssid, event->ssid_len);
            wifi_channel = event->channel;
            ESP_LOGI(TAG, "Connected to %s (channel %d)", current_ssid.c_str(), wifi_channel);
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW(TAG, "Disconnected from WiFi, reason: %d", event->reason);
            wifi_connected = false;
            internet_available = false;
            current_ip.clear();

            if (on_disconnected) {
                on_disconnected();
            }

            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
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
            xEventGroupSetBits(wifi_event_group, WIFI_SCAN_DONE_BIT);
            break;
    }
}

void WiFiManager::handleIPEvent(int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        current_ip = ip_str;

        ESP_LOGI(TAG, "Got IP address: %s", current_ip.c_str());
        wifi_connected = true;

        if (on_connected) {
            on_connected(current_ssid, current_ip);
        }

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t WiFiManager::initialize()
{
    ESP_LOGI(TAG, "Initializing WiFi manager...");

    // Create event group
    wifi_event_group = xEventGroupCreate();
    if (!wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

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

esp_err_t WiFiManager::connectToStoredNetwork(int index)
{
    if (!nvs_manager || index < 0 || index >= NUM_WIFI_CREDENTIALS) {
        return ESP_ERR_INVALID_ARG;
    }

    const WifiCredential& cred = nvs_manager->wifiCredentials[index];
    if (cred.ssid.empty()) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Connecting to: %s", cred.ssid.c_str());

    wifi_config_t sta_config = {};
    strncpy((char*)sta_config.sta.ssid, cred.ssid.c_str(), sizeof(sta_config.sta.ssid) - 1);
    strncpy((char*)sta_config.sta.password, cred.password.c_str(), sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdTRUE, pdFALSE,
                                            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to %s", cred.ssid.c_str());
        nvs_manager->lastConnectedNetworkIndex = index;
        nvs_manager->saveWiFiCredentials();
        return ESP_OK;
    }

    // Stop ongoing STA retries for this credential; update() will retry later.
    esp_wifi_disconnect();
    ESP_LOGW(TAG, "Failed to connect to %s", cred.ssid.c_str());
    return ESP_ERR_WIFI_NOT_CONNECT;
}

esp_err_t WiFiManager::connectToWiFi()
{
    if (!nvs_manager) {
        ESP_LOGE(TAG, "NVS manager not set");
        return ESP_ERR_INVALID_STATE;
    }

    // Enable STA only when we are about to actively try a connection.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    last_connect_attempt = esp_timer_get_time() / 1000;

    bool hasStoredNetworks = false;
    for (int i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        if (!nvs_manager->wifiCredentials[i].ssid.empty()) {
            hasStoredNetworks = true;
            break;
        }
    }

    // First try last connected network
    int lastIndex = nvs_manager->lastConnectedNetworkIndex;
    if (lastIndex >= 0 && lastIndex < NUM_WIFI_CREDENTIALS) {
        if (connectToStoredNetwork(lastIndex) == ESP_OK) {
            checkInternetConnectivity();
            return ESP_OK;
        }
    }

    // Try other stored networks
    for (int i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        if (i == lastIndex) continue;  // Already tried

        if (connectToStoredNetwork(i) == ESP_OK) {
            checkInternetConnectivity();
            return ESP_OK;
        }
    }

    if (!hasStoredNetworks && strlen(CONFIG_QNOB_WIFI_SSID) > 0) {
        ESP_LOGI(TAG, "Trying default WiFi from Kconfig: %s", CONFIG_QNOB_WIFI_SSID);

        wifi_config_t sta_config = {};
        strncpy((char*)sta_config.sta.ssid, CONFIG_QNOB_WIFI_SSID, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char*)sta_config.sta.password, CONFIG_QNOB_WIFI_PASSWORD, sizeof(sta_config.sta.password) - 1);
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_ERROR_CHECK(esp_wifi_connect());

        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                pdTRUE, pdFALSE,
                                                pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Connected to default WiFi SSID");
            nvs_manager->wifiCredentials[0].ssid = CONFIG_QNOB_WIFI_SSID;
            nvs_manager->wifiCredentials[0].password = CONFIG_QNOB_WIFI_PASSWORD;
            nvs_manager->wifiCredentials[0].remember = true;
            nvs_manager->lastConnectedNetworkIndex = 0;
            nvs_manager->saveWiFiCredentials();
            checkInternetConnectivity();
            return ESP_OK;
        }

        esp_wifi_disconnect();
        ESP_LOGW(TAG, "Failed to connect to default WiFi SSID");
    }

    // No successful STA connection: keep AP only to avoid repeated STA warning spam.
    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_LOGW(TAG, "Failed to connect to any stored network");
    return ESP_ERR_WIFI_NOT_CONNECT;
}

void WiFiManager::disconnect()
{
    esp_wifi_disconnect();
    wifi_connected = false;
    internet_available = false;
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

    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);  // Non-blocking
    if (ret == ESP_OK) {
        last_scan_time = esp_timer_get_time() / 1000;
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
    if (!wifi_connected) return -100;

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return -100;
}

std::string WiFiManager::getIPAddress() const
{
    return current_ip;
}

std::string WiFiManager::getSSID() const
{
    return current_ssid;
}

bool WiFiManager::checkInternetConnectivity()
{
    if (!wifi_connected) {
        internet_available = false;
        return false;
    }

    // Try to resolve a well-known DNS
    ip_addr_t dns_addr;
    esp_err_t err = dns_gethostbyname("www.google.com", &dns_addr, nullptr, nullptr);

    if (err == ESP_OK || err == ERR_INPROGRESS) {
        // DNS resolution started/succeeded
        internet_available = true;
        ESP_LOGI(TAG, "Internet connectivity confirmed");
        return true;
    }

    // Try a simple TCP connection to Google DNS
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        internet_available = false;
        return false;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &dest_addr.sin_addr);

    // Set non-blocking with timeout
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    int result = connect(sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    close(sock);

    internet_available = (result == 0);

    if (internet_available) {
        ESP_LOGI(TAG, "Internet connectivity confirmed via TCP");
    } else {
        ESP_LOGW(TAG, "No internet connectivity");
    }

    return internet_available;
}

void WiFiManager::update()
{
    if (!initialized) return;

    int64_t current_time = esp_timer_get_time() / 1000;

    // Check WiFi connection status
    wifi_ap_record_t ap_info;
    bool currently_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);

    if (currently_connected && !wifi_connected) {
        // Just connected
        wifi_connected = true;
        ESP_LOGI(TAG, "WiFi reconnected to: %s", (char*)ap_info.ssid);
        checkInternetConnectivity();
    } else if (!currently_connected && wifi_connected) {
        // Just disconnected
        wifi_connected = false;
        internet_available = false;
        ESP_LOGW(TAG, "WiFi disconnected");
        if (on_disconnected) {
            on_disconnected();
        }
    }

    // Periodic tasks
    if (!wifi_connected && (current_time - last_connect_attempt > WIFI_SCAN_INTERVAL_MS)) {
        // Try to reconnect
        ESP_LOGI(TAG, "Attempting to reconnect to WiFi...");
        connectToWiFi();
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

static std::string g_ip_buffer;

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
    g_ip_buffer = static_cast<WiFiManager*>(handle)->getIPAddress();
    return g_ip_buffer.c_str();
}

}
