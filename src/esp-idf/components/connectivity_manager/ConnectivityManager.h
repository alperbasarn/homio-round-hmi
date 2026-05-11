#pragma once

#include "esp_err.h"

// T-01 scaffold. The ConnectivityManager will become the single owner of the
// radio (WiFi STA/AP + BLE adv/scan) once T-03..T-06 land. For now this header
// only declares the singleton accessor and a no-op begin() so subsequent
// tickets can introduce the task, event queue, and snapshot incrementally
// without re-shaping the public surface every time.
class ConnectivityManager {
public:
    static ConnectivityManager& instance();

    // No-op in T-01. T-03 will spawn the FreeRTOS task + queue here.
    esp_err_t begin();

private:
    ConnectivityManager() = default;
    ConnectivityManager(const ConnectivityManager&) = delete;
    ConnectivityManager& operator=(const ConnectivityManager&) = delete;

    bool initialized_ = false;
};
