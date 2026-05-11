#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ConnectivitySnapshot.h"

// State alias for the ConnMgr FSM — mirrors ConnectivitySnapshot::WifiState.
using ConnMgrState = ConnectivitySnapshot::WifiState;

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
};

// ConnectivityManager is the single owner of the radio (WiFi STA/AP + BLE
// adv/scan) once T-03..T-06 land. It runs a dedicated FreeRTOS task that
// owns an event queue and drives a state machine. Other tasks post events via
// postEvent() — they never mutate state directly.
class ConnectivityManager {
public:
    static ConnectivityManager& instance();

    // Spawns the "ConnMgr" FreeRTOS task + creates the event queue, seeds the
    // published snapshot, and posts EV_BOOT_DONE to kick the state machine.
    esp_err_t begin();

    // Post an event from any task or ISR context (safe to call from WiFi event
    // handlers, timers, and portal code). Returns false if the queue is full
    // or not yet created.
    bool postEvent(ConnMgrEvent ev);

    // Lock-free snapshot read. Safe to call from any task (HTTP handlers,
    // display, command router). Returns a by-value copy via a single-writer
    // seqlock — readers retry briefly if a publish races with them.
    ConnectivitySnapshot getSnapshot() const;

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

    TaskHandle_t  taskHandle_      = nullptr;
    QueueHandle_t queue_           = nullptr;
    ConnMgrState  state_           = ConnMgrState::Boot;
    uint8_t       ap_client_count_ = 0;
};
