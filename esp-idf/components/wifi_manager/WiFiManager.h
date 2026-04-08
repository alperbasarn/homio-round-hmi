#pragma once

#include <string>
#include <functional>
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// Forward declaration
class NVSManager;

// WiFi event callback types
using WiFiConnectedCallback = std::function<void(const std::string& ssid, const std::string& ip)>;
using WiFiDisconnectedCallback = std::function<void()>;

// Configuration
#define WIFI_AP_SSID_PREFIX     "QNOB-"
#define WIFI_AP_PASSWORD        "knobsetup"
#define WIFI_AP_CHANNEL         1
#define WIFI_AP_MAX_CONNECTIONS 4

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_SCAN_INTERVAL_MS   60000
#define WIFI_AP_CHECK_INTERVAL_MS 30000

// Event bits
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1
#define WIFI_SCAN_DONE_BIT      BIT2

#ifdef __cplusplus

class WiFiManager {
public:
    WiFiManager(NVSManager* nvsManager);
    ~WiFiManager();

    // Initialization
    esp_err_t initialize();

    // Connection methods
    esp_err_t connectToWiFi();
    esp_err_t startAPMode();
    esp_err_t startAPSTAMode();
    void disconnect();

    // Status methods
    bool isConnected() const { return wifi_connected; }
    bool isAPModeActive() const { return ap_mode_active; }
    int getSignalStrength();  // Returns 0-4 (like phone bars)
    std::string getIPAddress() const;
    std::string getSSID() const;
    int8_t getRSSI() const;

    // Scanning
    esp_err_t scanNetworks();
    int getScanResultCount() const { return scan_result_count; }

    // Callbacks
    void setConnectedCallback(WiFiConnectedCallback callback) { on_connected = callback; }
    void setDisconnectedCallback(WiFiDisconnectedCallback callback) { on_disconnected = callback; }

    // Update - call regularly
    void update();

    // Internet connectivity check
    bool checkInternetConnectivity();
    bool isInternetAvailable() const { return internet_available; }

private:
    NVSManager* nvs_manager;
    EventGroupHandle_t wifi_event_group;

    bool initialized;
    bool wifi_connected;
    bool ap_mode_active;
    bool internet_available;
    int scan_result_count;

    std::string current_ssid;
    std::string current_ip;
    int wifi_channel;

    int64_t last_ap_check_time;
    int64_t last_scan_time;
    int64_t last_connect_attempt;

    // Callbacks
    WiFiConnectedCallback on_connected;
    WiFiDisconnectedCallback on_disconnected;

    // Event handler
    static void eventHandler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data);
    void handleWiFiEvent(int32_t event_id, void* event_data);
    void handleIPEvent(int32_t event_id, void* event_data);

    // Helper methods
    esp_err_t initWiFi();
    esp_err_t connectToStoredNetwork(int index);
    std::string generateAPName();
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

// C-compatible interface
typedef void* wifi_manager_handle_t;

wifi_manager_handle_t wifi_manager_create(void* nvs_manager);
void wifi_manager_destroy(wifi_manager_handle_t handle);
esp_err_t wifi_manager_init(wifi_manager_handle_t handle);
esp_err_t wifi_manager_connect(wifi_manager_handle_t handle);
bool wifi_manager_is_connected(wifi_manager_handle_t handle);
const char* wifi_manager_get_ip(wifi_manager_handle_t handle);

#ifdef __cplusplus
}
#endif
