#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"

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

#define WIFI_CONNECT_TIMEOUT_MS   15000   // per candidate network
#define WIFI_RETRY_INTERVAL_MS    60000   // between whole connect sequences while offline
#define WIFI_SCAN_INTERVAL_MS     WIFI_RETRY_INTERVAL_MS
#define WIFI_AP_CHECK_INTERVAL_MS 30000

#ifdef __cplusplus

// Station/AP manager.
//
// NET-30: nothing here blocks the calling task any more. connectToWiFi() requests a connect
// sequence; update(), on the network task, tries one candidate network per WIFI_CONNECT_TIMEOUT_MS
// and reacts to the driver's events, so no network-task iteration waits on the radio.
// NET-09: the internet-availability flag lives in InternetHandler, the single owner; this class
// only knows about the link.
// NET-10: the SSID and IP are fixed character arrays written under a spinlock by the event task;
// the getters return copies, so no std::string member is mutated across tasks.
class WiFiManager {
public:
    WiFiManager(NVSManager* nvsManager);
    ~WiFiManager();

    // Initialization
    esp_err_t initialize();

    // Connection methods
    esp_err_t connectToWiFi();   // requests a sequence; returns ESP_OK once queued
    void requestConnect();       // safe from any task
    esp_err_t startAPMode();
    esp_err_t startAPSTAMode();
    void disconnect();

    // Status methods
    bool isConnected() const { return wifi_connected.load(std::memory_order_acquire); }
    bool isConnecting() const { return phase == ConnectPhase::Trying; }
    bool isAPModeActive() const { return ap_mode_active; }
    int getSignalStrength();     // 0-4, the driver's own mapping (the model maps RSSI itself)
    std::string getIPAddress() const;
    std::string getSSID() const;
    int8_t getRSSI() const;

    // Scanning
    esp_err_t scanNetworks();
    int getScanResultCount() const { return scan_result_count; }
    bool scanDone() const { return scan_done.load(std::memory_order_acquire); }

    // Callbacks
    void setConnectedCallback(WiFiConnectedCallback callback) { on_connected = callback; }
    void setDisconnectedCallback(WiFiDisconnectedCallback callback) { on_disconnected = callback; }

    // Update - call regularly from the network task; never blocks
    void update();

private:
    enum class ConnectPhase : uint8_t { Idle, Trying, Done };
    static constexpr int kMaxCandidates = 4;      // NUM_WIFI_CREDENTIALS + the Kconfig default
    static constexpr int kKconfigCandidate = -1;

    NVSManager* nvs_manager;

    bool initialized;
    std::atomic<bool> wifi_connected;
    std::atomic<bool> sta_fail_event;
    std::atomic<bool> connect_requested;
    std::atomic<bool> scan_done;
    bool ap_mode_active;
    bool last_reported_connected;
    int scan_result_count;

    mutable portMUX_TYPE text_lock;
    char current_ssid[33];
    char current_ip[16];
    int wifi_channel;

    int64_t last_ap_check_time;
    int64_t last_scan_time;
    int64_t last_connect_attempt;
    int64_t attempt_started_ms;

    ConnectPhase phase;
    int candidates[kMaxCandidates];
    int candidate_count;
    int candidate_index;
    int active_candidate;

    // Callbacks
    WiFiConnectedCallback on_connected;
    WiFiDisconnectedCallback on_disconnected;

    // Event handler
    static void eventHandler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data);
    void handleWiFiEvent(int32_t event_id, void* event_data);
    void handleIPEvent(int32_t event_id, void* event_data);

    // Connect sequence (network task)
    void beginSequence(int64_t now);
    bool startCandidate(int position, int64_t now);
    void serviceSequence(int64_t now);
    void finishSequence(bool connected, int64_t now);

    // Helper methods
    std::string generateAPName();
    void setText(char* dst, size_t capacity, const char* src, size_t len);
    std::string readText(const char* src) const;
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
