#pragma once

#include <string>
#include <functional>
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "ConnectivityManager.h"

// Forward declaration
class NVSManager;
class SetupPortal;

// WiFi event callback types
using WiFiConnectedCallback = std::function<void(const std::string& ssid, const std::string& ip)>;
using WiFiDisconnectedCallback = std::function<void()>;

// Configuration
#define WIFI_AP_CHANNEL         1
#define WIFI_AP_MAX_CONNECTIONS 4

#define WIFI_CONNECT_TIMEOUT_MS   15000
#define WIFI_SCAN_INTERVAL_MS     60000
#define WIFI_AP_CHECK_INTERVAL_MS 30000

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

    // Status methods — state is owned by ConnectivityManager; read via snapshot.
    bool isConnected() const {
        return ConnectivityManager::instance().getSnapshot().wifi_state
               == ConnMgrState::StaConnected;
    }
    bool isAPModeActive() const {
        return ConnectivityManager::instance().getSnapshot().wifi_state
               != ConnMgrState::Boot;
    }
    int getSignalStrength();  // Returns 0-4 (like phone bars)
    std::string getIPAddress() const;
    std::string getAPIPAddress() const;
    std::string getSSID() const;
    std::string getAPSSID() const;
    std::string getAPPassword() const;
    int8_t getRSSI() const;

    // Scanning
    esp_err_t scanNetworks();
    int getScanResultCount() const { return scan_result_count; }
    esp_err_t connectToNetwork(const std::string& ssid, const std::string& password, bool remember = true);
    esp_err_t syncAccessPointConfig();
    esp_err_t saveCurrentConnectionAsStaticIP();
    void setSetupPortalScreenControlCallback(std::function<bool(const std::string&)> callback);
    void setSetupPortalScreenStatusCallback(std::function<std::string(void)> callback);
    void setSetupPortalOtaConfigUpdatedCallback(std::function<void(void)> callback);
    void setSetupPortalOtaStatusCallback(std::function<std::string(void)> callback);
    void setSetupPortalOtaActionCallback(std::function<esp_err_t(const std::string&)> callback);
    void setSetupPortalDeviceInfoStatusCallback(std::function<std::string(void)> callback);
    void setSetupPortalCommandCallback(std::function<void(const std::string&)> callback);
    void setSetupPortalBtScanResultsCallback(std::function<std::string(void)> callback);

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

    bool initialized;
    bool internet_available;
    bool prev_sta_connected_;    // edge detection for callbacks in update()
    bool prev_sta_connecting_;    // edge detection: StaConnecting → fail in update()
    bool prev_portal_guest_;      // edge detection: PortalGuestActive → off in update()
    bool scan_pending_connect_;   // scan was triggered by connectToWiFi(); on done → pick best
    int  connect_cred_index_;     // slot of the credential being attempted (-1 = none)
    int  connect_creds_tried_;    // kept for compatibility; not used for cycling in T-20
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
    SetupPortal* setup_portal;
    std::function<bool(const std::string&)> portal_screen_control_callback;
    std::function<std::string(void)> portal_screen_status_callback;
    std::function<void(void)> portal_ota_config_updated_callback;
    std::function<std::string(void)> portal_ota_status_callback;
    std::function<esp_err_t(const std::string&)> portal_ota_action_callback;
    std::function<std::string(void)> portal_device_info_status_callback;
    std::function<void(const std::string&)> portal_command_callback;
    std::function<std::string(void)> portal_bt_scan_results_callback;

    // Event handler
    static void eventHandler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data);
    void handleWiFiEvent(int32_t event_id, void* event_data);
    void handleIPEvent(int32_t event_id, void* event_data);

    // Helper methods
    esp_err_t initWiFi();
    esp_err_t connectToStoredNetwork(int index);
    esp_err_t startScanThenConnect_();  // kick non-blocking scan; pick best on SCAN_DONE
    esp_err_t applySTAIPConfig(const std::string& targetSsid = "");
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
