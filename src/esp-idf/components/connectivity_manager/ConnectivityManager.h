#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"

#include "ConnectivitySnapshot.h"

// ConnectivityManager will become the single owner of the radio (WiFi STA/AP
// + BLE adv/scan) once T-03..T-06 land. Today (T-02) it exposes the
// read-only state snapshot that the rest of the system will pull from
// instead of chaining `wifiManager->isConnected()` /
// `bluetoothManager->isHidConnected()` queries.
class ConnectivityManager {
public:
    static ConnectivityManager& instance();

    // T-03 will spawn the FreeRTOS task + event queue here. For now begin()
    // just seeds the published snapshot once so version == 1 marks
    // "ConnMgr has been initialised."
    esp_err_t begin();

    // Lock-free snapshot read. Safe to call from any task (HTTP handlers,
    // display, command router). Returns a by-value copy via a single-writer
    // seqlock — readers retry briefly if a publish races with them.
    ConnectivitySnapshot getSnapshot() const;

private:
    ConnectivityManager() = default;
    ConnectivityManager(const ConnectivityManager&) = delete;
    ConnectivityManager& operator=(const ConnectivityManager&) = delete;

    // Single-writer seqlock publish. Even seq values indicate "snapshot_ is
    // stable"; odd values indicate "writer is mid-update, retry the read."
    // Called from begin() today and from event handlers in T-03+.
    void publishSnapshot(const ConnectivitySnapshot& next);

    ConnectivitySnapshot snapshot_{};
    mutable std::atomic<uint32_t> seq_{0};
    bool initialized_ = false;
};
