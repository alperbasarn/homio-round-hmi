#include "WiFiManager.h"
#include "NVSManager.h"
#include "SetupPortal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <algorithm>
#include <cstring>

static const char *TAG = "WiFiManager";

// Static instance pointer for event handler
static WiFiManager* s_wifi_manager = nullptr;

WiFiManager::WiFiManager(NVSManager* nvsManager)
    : nvs_manager(nvsManager),
      initialized(false),
      internet_available(false),
      prev_sta_connected_(false),
      prev_sta_connecting_(false),
      prev_portal_guest_(false),
      scan_pending_connect_(false),
      connect_cred_index_(-1),
      connect_creds_tried_(0),
      scan_result_count(0),
      wifi_channel(WIFI_AP_CHANNEL),
      last_ap_check_time(0),
      last_scan_time(0),
      last_connect_attempt(0),
      on_connected(nullptr),
            on_disconnected(nullptr),
            setup_portal(nullptr)
{
    s_wifi_manager = this;
}

WiFiManager::~WiFiManager()
{
    if (setup_portal != nullptr) {
        setup_portal->stop();
        delete setup_portal;
        setup_portal = nullptr;
    }

    if (initialized) {
        ConnectivityManager::instance().stopWifiStack();
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
            ESP_LOGI(TAG, "Connected to %s", current_ssid.c_str());
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW(TAG, "Disconnected from WiFi, reason: %d", event->reason);
            current_ip.clear();
            ConnectivityManager::instance().postEvent(ConnMgrEvent::EV_STA_DISCONNECTED);
            xEventGroupSetBits(ConnectivityManager::instance().getWifiEventGroup(), kWifiFailBit);
            break;
        }

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started");
            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "AP stopped");
            break;

        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
            ESP_LOGI(TAG, "Station " MACSTR " joined AP", MAC2STR(event->mac));
            ConnectivityManager::instance().postEvent(ConnMgrEvent::EV_AP_CLIENT_JOINED);
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
            ESP_LOGI(TAG, "Station " MACSTR " left AP", MAC2STR(event->mac));
            ConnectivityManager::instance().postEvent(ConnMgrEvent::EV_AP_CLIENT_LEFT);
            // STA retry is deferred by ConnMgr's 30 s debounce timer (T-09);
            // do NOT reset last_connect_attempt here.
            break;
        }

        case WIFI_EVENT_SCAN_DONE: {
            ESP_LOGI(TAG, "WiFi scan completed");
            xEventGroupSetBits(ConnectivityManager::instance().getWifiEventGroup(), kWifiScanDoneBit);

            if (!scan_pending_connect_ || !nvs_manager) {
                break;
            }
            scan_pending_connect_ = false;

            // Read scan results and cross-reference with saved credentials.
            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);
            if (ap_count == 0) {
                ESP_LOGW(TAG, "Scan found no APs; staying in AP mode");
                break;
            }

            // Cap to a reasonable maximum to avoid stack pressure.
            constexpr uint16_t kMaxAPs = 20;
            if (ap_count > kMaxAPs) ap_count = kMaxAPs;
            wifi_ap_record_t records[kMaxAPs];
            if (esp_wifi_scan_get_ap_records(&ap_count, records) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read scan records");
                break;
            }

            // Find the saved credential with the strongest (highest) RSSI among visible APs.
            int best_cred = -1;
            int8_t best_rssi = INT8_MIN;
            for (int ci = 0; ci < NUM_WIFI_CREDENTIALS; ++ci) {
                const auto& cred = nvs_manager->wifiCredentials[ci];
                if (cred.ssid.empty()) continue;
                for (uint16_t ai = 0; ai < ap_count; ++ai) {
                    const char* ap_ssid = reinterpret_cast<const char*>(records[ai].ssid);
                    if (cred.ssid == ap_ssid && records[ai].rssi > best_rssi) {
                        best_rssi  = records[ai].rssi;
                        best_cred  = ci;
                    }
                }
            }

            if (best_cred >= 0) {
                ESP_LOGI(TAG, "Best visible saved network: %s (RSSI %d dBm)",
                         nvs_manager->wifiCredentials[best_cred].ssid.c_str(), best_rssi);
                connect_creds_tried_ = 0;
                connectToStoredNetwork(best_cred);
            } else {
                ESP_LOGW(TAG, "No saved network in range; staying in AP mode");
            }
            break;
        }
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

        ConnectivityManager::instance().postEvent(ConnMgrEvent::EV_STA_GOT_IP);
        xEventGroupSetBits(ConnectivityManager::instance().getWifiEventGroup(), kWifiConnectedBit);
    }
}

esp_err_t WiFiManager::initialize()
{
    ESP_LOGI(TAG, "Initializing WiFi manager...");

    // Initialize networking stack in an idempotent way so AP/portal also work
    // when another subsystem already performed part of the setup.
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }

    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == nullptr) {
        esp_netif_create_default_wifi_sta();
    }
    if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") == nullptr) {
        esp_netif_create_default_wifi_ap();
    }

    // WiFi stack init (esp_wifi_*) delegated to ConnMgr so that no esp_wifi_*
    // symbols are referenced outside the connectivity_manager component.
    err = ConnectivityManager::instance().initWifiStack();
    if (err != ESP_OK) {
        return err;
    }

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &eventHandler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &eventHandler, this, nullptr));

    initialized = true;
    current_ap_ssid = generateAPName();
    current_ap_password = nvs_manager ? nvs_manager->accessPointPassword : "";
    current_ap_ip = "192.168.4.1";

    err = startAPMode();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AP mode: %s", esp_err_to_name(err));
        return err;
    }

    if (setup_portal == nullptr) {
        setup_portal = new SetupPortal(this, nvs_manager);
        if (setup_portal != nullptr) {
            if (portal_screen_control_callback) {
                setup_portal->setScreenControlCallback(portal_screen_control_callback);
            }
            if (portal_screen_status_callback) {
                setup_portal->setScreenStatusCallback(portal_screen_status_callback);
            }
            if (portal_ota_config_updated_callback) {
                setup_portal->setOtaConfigUpdatedCallback(portal_ota_config_updated_callback);
            }
            if (portal_ota_status_callback) {
                setup_portal->setOtaStatusCallback(portal_ota_status_callback);
            }
            if (portal_ota_action_callback) {
                setup_portal->setOtaActionCallback(portal_ota_action_callback);
            }
            if (portal_device_info_status_callback) {
                setup_portal->setDeviceInfoStatusCallback(portal_device_info_status_callback);
            }
            if (portal_command_callback) {
                setup_portal->setCommandCallback(portal_command_callback);
            }
            if (portal_bt_scan_results_callback) {
                setup_portal->setBtScanResultsCallback(portal_bt_scan_results_callback);
            }
            err = setup_portal->start();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start setup portal: %s", esp_err_to_name(err));
                return err;
            }
        }
    }

    ESP_LOGI(TAG, "WiFi manager initialized");

    return ESP_OK;
}

std::string WiFiManager::generateAPName()
{
    if (nvs_manager && !nvs_manager->deviceName.empty()) {
        return nvs_manager->deviceName;
    }
    return "Homio-0000";
}

esp_err_t WiFiManager::startAPMode()
{
    std::string ap_name = generateAPName();
    if (ap_name.empty()) {
        ap_name = "Homio-0000";
    }
    if (ap_name.size() > 32) {
        ap_name.resize(32);
    }

    std::string ap_password = nvs_manager ? nvs_manager->accessPointPassword : "";
    if (!ap_password.empty() && (ap_password.size() < 8 || ap_password.size() > 63)) {
        ESP_LOGW(TAG, "Ignoring invalid AP password length (%u), starting open AP",
                 static_cast<unsigned>(ap_password.size()));
        ap_password.clear();
    }

    const bool use_password = ap_password.size() >= 8 && ap_password.size() <= 63;
    const int ap_channel = (wifi_channel >= 1 && wifi_channel <= 13) ? wifi_channel : WIFI_AP_CHANNEL;
    wifi_channel = ap_channel;

    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, ap_name.c_str(), sizeof(ap_config.ap.ssid) - 1);
    if (use_password) {
        strncpy((char*)ap_config.ap.password, ap_password.c_str(), sizeof(ap_config.ap.password) - 1);
    }
    ap_config.ap.ssid_len = ap_name.length();
    ap_config.ap.channel = ap_channel;
    ap_config.ap.max_connection = WIFI_AP_MAX_CONNECTIONS;
    ap_config.ap.authmode = use_password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_err_t err = ConnectivityManager::instance().configureAP(ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure AP %s: %s", ap_name.c_str(), esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "AP started: %s (channel %d)", ap_name.c_str(), ap_channel);
    current_ap_ssid = ap_name;
    current_ap_password = use_password ? ap_password : "";

    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif != nullptr) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            char ip_str[16] = {0};
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            current_ap_ip = ip_str;
        }
    }

    return ESP_OK;
}

esp_err_t WiFiManager::syncAccessPointConfig()
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return startAPMode();
}

esp_err_t WiFiManager::applySTAIPConfig(const std::string& targetSsid)
{
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == nullptr || nvs_manager == nullptr) {
        return ESP_OK;
    }

    if (!nvs_manager->staticIPEnabled) {
        esp_netif_dhcpc_start(sta_netif);
        return ESP_OK;
    }

    if (!nvs_manager->staticIPSSID.empty() && !targetSsid.empty() && nvs_manager->staticIPSSID != targetSsid) {
        esp_netif_dhcpc_start(sta_netif);
        return ESP_OK;
    }

    esp_netif_dhcpc_stop(sta_netif);

    esp_netif_ip_info_t ip_info = {};
    struct in_addr addr = {};
    if (inet_pton(AF_INET, nvs_manager->staticIP.c_str(), &addr) != 1) {
        ESP_LOGW(TAG, "Invalid static IP configuration, falling back to DHCP");
        esp_netif_dhcpc_start(sta_netif);
        return ESP_ERR_INVALID_ARG;
    }
    ip_info.ip.addr = addr.s_addr;

    if (inet_pton(AF_INET, nvs_manager->staticGateway.c_str(), &addr) != 1) {
        ESP_LOGW(TAG, "Invalid static IP configuration, falling back to DHCP");
        esp_netif_dhcpc_start(sta_netif);
        return ESP_ERR_INVALID_ARG;
    }
    ip_info.gw.addr = addr.s_addr;

    if (inet_pton(AF_INET, nvs_manager->staticSubnet.c_str(), &addr) != 1) {
        ESP_LOGW(TAG, "Invalid static IP configuration, falling back to DHCP");
        esp_netif_dhcpc_start(sta_netif);
        return ESP_ERR_INVALID_ARG;
    }
    ip_info.netmask.addr = addr.s_addr;

    esp_err_t err = esp_netif_set_ip_info(sta_netif, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set static IP info: %s", esp_err_to_name(err));
        esp_netif_dhcpc_start(sta_netif);
        return err;
    }

    esp_netif_dns_info_t dns = {};
    if (inet_pton(AF_INET, nvs_manager->staticDNS1.c_str(), &addr) == 1) {
        dns.ip.u_addr.ip4.addr = addr.s_addr;
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns);
    }
    if (inet_pton(AF_INET, nvs_manager->staticDNS2.c_str(), &addr) == 1) {
        dns.ip.u_addr.ip4.addr = addr.s_addr;
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_BACKUP, &dns);
    }

    ESP_LOGI(TAG, "Using static IP: %s", nvs_manager->staticIP.c_str());
    return ESP_OK;
}

esp_err_t WiFiManager::startAPSTAMode()
{
    ConnectivityManager::instance().setWifiMode(WIFI_MODE_APSTA);
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

    ConnectivityManager::instance().setWifiMode(WIFI_MODE_APSTA);
    applySTAIPConfig(cred.ssid);

    wifi_config_t sta_config = {};
    strncpy((char*)sta_config.sta.ssid, cred.ssid.c_str(), sizeof(sta_config.sta.ssid) - 1);
    strncpy((char*)sta_config.sta.password, cred.password.c_str(), sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = cred.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    // Non-blocking after T-08: result arrives via EV_STA_GOT_IP or timeout.
    ConnectivityManager::instance().startSTAConnect(sta_config);
    connect_cred_index_ = index;
    return ESP_OK;  // attempt launched
}

esp_err_t WiFiManager::startScanThenConnect_()
{
    scan_pending_connect_ = true;
    const esp_err_t err = ConnectivityManager::instance().startNonBlockingScan();
    if (err != ESP_OK) {
        // Scan failed to start — fall back to direct connect attempt if we have a last-known-good.
        scan_pending_connect_ = false;
        ESP_LOGW(TAG, "Scan start failed (%s); attempting direct connect", esp_err_to_name(err));
        if (nvs_manager) {
            int idx = nvs_manager->lastConnectedNetworkIndex;
            if (idx >= 0 && idx < NUM_WIFI_CREDENTIALS &&
                !nvs_manager->wifiCredentials[idx].ssid.empty()) {
                return connectToStoredNetwork(idx);
            }
        }
        return err;
    }
    ESP_LOGI(TAG, "WiFi scan started; will connect to strongest saved network on SCAN_DONE");
    return ESP_OK;
}

esp_err_t WiFiManager::connectToWiFi()
{
    if (!nvs_manager) {
        ESP_LOGE(TAG, "NVS manager not set");
        return ESP_ERR_INVALID_STATE;
    }

    // Ensure radio is in APSTA so AP keeps running during the scan.
    ConnectivityManager::instance().setWifiMode(WIFI_MODE_APSTA);
    last_connect_attempt = esp_timer_get_time() / 1000;
    connect_cred_index_ = -1;
    connect_creds_tried_ = 0;

    // Check whether there are any stored networks at all.
    bool hasStoredNetworks = false;
    for (int i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        if (!nvs_manager->wifiCredentials[i].ssid.empty()) {
            hasStoredNetworks = true;
            break;
        }
    }

    if (hasStoredNetworks) {
        // Scan first; SCAN_DONE handler picks the strongest visible saved network.
        return startScanThenConnect_();
    }

    if (strlen(CONFIG_QNOB_WIFI_SSID) > 0) {
        ESP_LOGI(TAG, "Trying default WiFi from Kconfig: %s", CONFIG_QNOB_WIFI_SSID);

        wifi_config_t sta_config = {};
        strncpy((char*)sta_config.sta.ssid, CONFIG_QNOB_WIFI_SSID, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char*)sta_config.sta.password, CONFIG_QNOB_WIFI_PASSWORD, sizeof(sta_config.sta.password) - 1);
        sta_config.sta.threshold.authmode =
            (strlen(CONFIG_QNOB_WIFI_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

        applySTAIPConfig(CONFIG_QNOB_WIFI_SSID);
        ConnectivityManager::instance().startSTAConnect(sta_config);
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t WiFiManager::connectToNetwork(const std::string& ssid, const std::string& password, bool remember)
{
    if (ssid.empty() || nvs_manager == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    int target = -1;
    for (int i = 0; i < NUM_WIFI_CREDENTIALS; ++i) {
        if (nvs_manager->wifiCredentials[i].ssid == ssid) {
            target = i;
            break;
        }
        if (target == -1 && nvs_manager->wifiCredentials[i].ssid.empty()) {
            target = i;
        }
    }
    if (target < 0) {
        target = 0;
    }

    nvs_manager->wifiCredentials[target].ssid = ssid;
    nvs_manager->wifiCredentials[target].password = password;
    nvs_manager->wifiCredentials[target].remember = remember;
    nvs_manager->lastConnectedNetworkIndex = target;
    nvs_manager->saveWiFiCredentials();

    ConnectivityManager::instance().disconnectSTA();
    connect_creds_tried_ = 0;
    return connectToStoredNetwork(target);  // non-blocking; result via snapshot
}

esp_err_t WiFiManager::saveCurrentConnectionAsStaticIP()
{
    const bool connected = ConnectivityManager::instance().getSnapshot().wifi_state
                           == ConnMgrState::StaConnected;
    if (!connected || current_ssid.empty() || nvs_manager == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info = {};
    esp_err_t err = esp_netif_get_ip_info(sta_netif, &ip_info);
    if (err != ESP_OK) {
        return err;
    }

    char ip_str[16] = {0};
    char gateway_str[16] = {0};
    char subnet_str[16] = {0};
    esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
    esp_ip4addr_ntoa(&ip_info.gw, gateway_str, sizeof(gateway_str));
    esp_ip4addr_ntoa(&ip_info.netmask, subnet_str, sizeof(subnet_str));

    std::string dns1 = nvs_manager->staticDNS1;
    std::string dns2 = nvs_manager->staticDNS2;
    esp_netif_dns_info_t dns_info = {};
    if (esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK &&
        dns_info.ip.type == ESP_IPADDR_TYPE_V4) {
        char dns_str[16] = {0};
        esp_ip4addr_ntoa(&dns_info.ip.u_addr.ip4, dns_str, sizeof(dns_str));
        dns1 = dns_str;
    }
    if (esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_BACKUP, &dns_info) == ESP_OK &&
        dns_info.ip.type == ESP_IPADDR_TYPE_V4) {
        char dns_str[16] = {0};
        esp_ip4addr_ntoa(&dns_info.ip.u_addr.ip4, dns_str, sizeof(dns_str));
        dns2 = dns_str;
    }

    return nvs_manager->saveStaticIPConfig(true,
                                           ip_str,
                                           gateway_str,
                                           subnet_str,
                                           dns1,
                                           dns2,
                                           current_ssid);
}

void WiFiManager::setSetupPortalScreenControlCallback(std::function<bool(const std::string&)> callback)
{
    portal_screen_control_callback = std::move(callback);
    if (setup_portal != nullptr) {
        setup_portal->setScreenControlCallback(portal_screen_control_callback);
    }
}

void WiFiManager::setSetupPortalScreenStatusCallback(std::function<std::string(void)> callback)
{
    portal_screen_status_callback = std::move(callback);
    if (setup_portal != nullptr) {
        setup_portal->setScreenStatusCallback(portal_screen_status_callback);
    }
}

void WiFiManager::setSetupPortalOtaConfigUpdatedCallback(std::function<void(void)> callback)
{
    portal_ota_config_updated_callback = std::move(callback);
    if (setup_portal != nullptr) {
        setup_portal->setOtaConfigUpdatedCallback(portal_ota_config_updated_callback);
    }
}

void WiFiManager::setSetupPortalOtaStatusCallback(std::function<std::string(void)> callback)
{
    portal_ota_status_callback = std::move(callback);
    if (setup_portal != nullptr) {
        setup_portal->setOtaStatusCallback(portal_ota_status_callback);
    }
}

void WiFiManager::setSetupPortalOtaActionCallback(std::function<esp_err_t(const std::string&)> callback)
{
    portal_ota_action_callback = std::move(callback);
    if (setup_portal != nullptr) {
        setup_portal->setOtaActionCallback(portal_ota_action_callback);
    }
}

void WiFiManager::setSetupPortalDeviceInfoStatusCallback(std::function<std::string(void)> callback)
{
    portal_device_info_status_callback = std::move(callback);
    if (setup_portal != nullptr) {
        setup_portal->setDeviceInfoStatusCallback(portal_device_info_status_callback);
    }
}

void WiFiManager::setSetupPortalCommandCallback(std::function<void(const std::string&)> callback)
{
    portal_command_callback = std::move(callback);
    if (setup_portal != nullptr) {
        setup_portal->setCommandCallback(portal_command_callback);
    }
}

void WiFiManager::setSetupPortalBtScanResultsCallback(std::function<std::string(void)> callback)
{
    portal_bt_scan_results_callback = std::move(callback);
    if (setup_portal != nullptr) {
        setup_portal->setBtScanResultsCallback(portal_bt_scan_results_callback);
    }
}

void WiFiManager::disconnect()
{
    ConnectivityManager::instance().disconnectSTA();
    internet_available = false;
}

esp_err_t WiFiManager::scanNetworks()
{
    esp_err_t ret = ConnectivityManager::instance().startNonBlockingScan();
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
    const bool connected = ConnectivityManager::instance().getSnapshot().wifi_state
                           == ConnMgrState::StaConnected;
    if (!connected) return -100;

    wifi_ap_record_t ap_info;
    if (ConnectivityManager::instance().getStaApInfo(&ap_info)) {
        return ap_info.rssi;
    }
    return -100;
}

std::string WiFiManager::getIPAddress() const
{
    return current_ip;
}

std::string WiFiManager::getAPIPAddress() const
{
    return current_ap_ip;
}

std::string WiFiManager::getSSID() const
{
    return current_ssid;
}

std::string WiFiManager::getAPSSID() const
{
    return current_ap_ssid;
}

std::string WiFiManager::getAPPassword() const
{
    return current_ap_password;
}

bool WiFiManager::checkInternetConnectivity()
{
    const bool connected = ConnectivityManager::instance().getSnapshot().wifi_state
                           == ConnMgrState::StaConnected;
    if (!connected) {
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

    const int64_t current_time = esp_timer_get_time() / 1000;
    const auto    snap         = ConnectivityManager::instance().getSnapshot();
    const bool    sta_connected =
        (snap.wifi_state == ConnMgrState::StaConnected);
    const bool portal_guest_active =
        (snap.wifi_state == ConnMgrState::PortalGuestActive);

    // Edge-detect STA connected → fire application callbacks.
    if (sta_connected && !prev_sta_connected_) {
        prev_sta_connected_ = true;
        internet_available = false;
        // Save the credential that succeeded.
        if (connect_cred_index_ >= 0 && nvs_manager) {
            nvs_manager->lastConnectedNetworkIndex = connect_cred_index_;
            nvs_manager->saveWiFiCredentials();
        }
        connect_cred_index_   = -1;
        connect_creds_tried_  = 0;
        checkInternetConnectivity();
        if (on_connected) {
            on_connected(current_ssid, current_ip);
        }
    } else if (!sta_connected && prev_sta_connected_) {
        prev_sta_connected_ = false;
        internet_available = false;
        if (on_disconnected) {
            on_disconnected();
        }
    }

    // Edge-detect PortalGuestActive → off (debounce expired): reset backoff so
    // update() triggers connectToWiFi() promptly on the next loop.
    if (prev_portal_guest_ && !portal_guest_active && !sta_connected) {
        ESP_LOGI(TAG, "Portal guest gone; scheduling immediate STA retry");
        last_connect_attempt = 0;
    }
    prev_portal_guest_ = portal_guest_active;

    // Edge-detect StaConnecting → failure: retry via a new scan.
    const bool sta_connecting = (snap.wifi_state == ConnMgrState::StaConnecting);
    if (prev_sta_connecting_ && !sta_connecting && !sta_connected) {
        // Connect attempt failed. Trigger a fresh scan so the next try
        // still picks the strongest available saved network.
        if (!scan_pending_connect_) {
            ESP_LOGI(TAG, "STA connect failed; retrying via scan");
            startScanThenConnect_();
        }
    }
    prev_sta_connecting_ = sta_connecting;

    // Periodic tasks
    if (!sta_connected && !portal_guest_active &&
        (current_time - last_connect_attempt > WIFI_SCAN_INTERVAL_MS)) {
        // Try to reconnect
        ESP_LOGI(TAG, "Attempting to reconnect to WiFi...");
        connectToWiFi();
    }

    // Check AP status
    if (current_time - last_ap_check_time > WIFI_AP_CHECK_INTERVAL_MS) {
        last_ap_check_time = current_time;

        const wifi_mode_t mode = ConnectivityManager::instance().getWifiMode();

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
