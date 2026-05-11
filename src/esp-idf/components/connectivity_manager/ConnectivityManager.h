#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ConnectivitySnapshot.h"

// State alias for the ConnMgr FSM — mirrors ConnectivitySnapshot::WifiState.
using ConnMgrState = ConnectivitySnapshot::WifiState;

// WiFi event-group bit definitions. Owned by ConnMgr (T-05+); event handlers
// in WiFiManager call getWifiEventGroup() to set them.
static constexpr EventBits_t kWifiConnectedBit = BIT0;
static constexpr EventBits_t kWifiFailBit       = BIT1;
static constexpr EventBits_t kWifiScanDoneBit   = BIT2;

// Events posted to the ConnMgr queue by WiFi/BT event handlers, timers, and
// user actions. T-03 only logs them; T-04+ will translate them into radio
// calls.
enum class ConnMgrEvent : uint8_t {
    EV_BOOT_DONE,
    EV_STA_GOT_IP,
    EV_STA_DISCONNECTED,
    EV_AP_CLIENT_JOINED,
    EV_AP_CLIENT_LEFT,
    EV_USER_REQUESTED_CONNECT,
    EV_BACKOFF_TIMER,
    EV_CONNECT_TIMEOUT,
    EV_BT_SCAN_REQUESTED,
    EV_BT_ENABLE_REQUESTED,
    EV_PORTAL_ENABLE_REQUESTED,
    // T-06: low-level BLE GAP dispatch — only executed inside runTask()
    EV_BT_ADV_START,
    EV_BT_ADV_STOP,
    EV_BT_SCAN_START,
};

// ConnectivityManager is the single owner of the radio (WiFi STA/AP + BLE
// adv/scan) once T-03..T-06 land. It runs a dedicated FreeRTOS task that
// owns an event queue and drives a state machine. Other tasks post events via
// postEvent() — they never mutate state directly.
//
// T-05: all esp_wifi_* calls are delegated through this class so that no
// other component references the esp_wifi_* symbol namespace directly.
class ConnectivityManager {
public:
    static ConnectivityManager& instance();

    // Spawns the "ConnMgr" FreeRTOS task + creates the event queue and the
    // WiFi event group, seeds the published snapshot, and posts EV_BOOT_DONE.
    esp_err_t begin();

    // Post an event from any task or ISR context (safe to call from WiFi event
    // handlers, timers, and portal code). Returns false if the queue is full
    // or not yet created.
    bool postEvent(ConnMgrEvent ev);

    // Lock-free snapshot read. Safe to call from any task (HTTP handlers,
    // display, command router). Returns a by-value copy via a single-writer
    // seqlock — readers retry briefly if a publish races with them.
    ConnectivitySnapshot getSnapshot() const;

    // ── BLE GAP delegation (T-06) ─────────────────────────────────────────
    // BluetoothManager calls these instead of esp_ble_gap_* directly.
    // Stores params and posts the corresponding event to the queue so the
    // actual GAP call happens inside runTask() on the ConnMgr task.
    void bleRequestAdvertisingStart(const esp_ble_adv_params_t& params);
    void bleRequestAdvertisingStop();
    void bleRequestScanStart(uint32_t durationSec);

    // ── WiFi event group ────────────────────────────────────────────────────
    // WiFiManager event handlers call this to set connection result bits
    // (kWifiConnectedBit, kWifiFailBit, kWifiScanDoneBit).
    EventGroupHandle_t getWifiEventGroup() const { return event_group_; }

    // ── WiFi stack lifecycle ─────────────────────────────────────────────────
    // Called from WiFiManager::initialize() / destructor so that all
    // esp_wifi_* symbols remain inside this component.
    esp_err_t initWifiStack();
    void      stopWifiStack();

    // ── Radio mutators (synchronous) ─────────────────────────────────────────
    // Replace direct esp_wifi_* calls in WiFiManager and SetupPortal.

    // Configure and apply an AP config; calls esp_wifi_set_config(WIFI_IF_AP).
    esp_err_t configureAP(const wifi_config_t& cfg);

    // Set APSTA mode, apply STA config, connect, and block until IP or fail
    // (up to WIFI_CONNECT_TIMEOUT_MS). Returns ESP_OK on success.
    esp_err_t connectSTA(const wifi_config_t& cfg);

    // esp_wifi_disconnect().
    void disconnectSTA();

    // esp_wifi_set_mode() / esp_wifi_get_mode().
    void        setWifiMode(wifi_mode_t mode);
    wifi_mode_t getWifiMode();

    // Non-blocking scan kick-off (WiFiManager::scanNetworks path).
    esp_err_t startNonBlockingScan();

    // Blocking scan + record retrieval (SetupPortal::renderScanJson path).
    esp_err_t syncScan(std::vector<wifi_ap_record_t>& out);

    // esp_wifi_ap_get_sta_list() / esp_wifi_sta_get_ap_info() wrappers.
    bool getApStaList(wifi_sta_list_t* out);
    bool getStaApInfo(wifi_ap_record_t* out);

private:
    ConnectivityManager() = default;
    ConnectivityManager(const ConnectivityManager&) = delete;
    ConnectivityManager& operator=(const ConnectivityManager&) = delete;

    // Static trampoline required by xTaskCreate.
    static void connMgrTask(void* arg);
    // Actual task body — runs on the ConnMgr task.
    void runTask();

    // Single-writer seqlock publish. Even seq values indicate "snapshot_ is
    // stable"; odd values indicate "writer is mid-update, retry the read."
    // Called only from the ConnMgr task.
    void publishSnapshot(const ConnectivitySnapshot& next);

    ConnectivitySnapshot snapshot_{};
    mutable std::atomic<uint32_t> seq_{0};
    bool initialized_ = false;

    TaskHandle_t       taskHandle_      = nullptr;
    QueueHandle_t      queue_           = nullptr;
    EventGroupHandle_t event_group_     = nullptr;
    ConnMgrState       state_           = ConnMgrState::Boot;
    uint8_t            ap_client_count_ = 0;

    // BLE GAP params — written before posting EV_BT_ADV_START/EV_BT_SCAN_START;
    // read only inside runTask(). FreeRTOS queue ops provide the memory barrier.
    esp_ble_adv_params_t ble_adv_params_{};
    uint32_t             ble_scan_duration_ = 5;
};
