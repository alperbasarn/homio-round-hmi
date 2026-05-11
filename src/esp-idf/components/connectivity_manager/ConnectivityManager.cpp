#include "ConnectivityManager.h"

#include "esp_log.h"

namespace {
constexpr const char* TAG = "ConnMgr";
}

ConnectivityManager& ConnectivityManager::instance() {
    static ConnectivityManager manager;
    return manager;
}

esp_err_t ConnectivityManager::begin() {
    if (initialized_) {
        return ESP_OK;
    }
    initialized_ = true;
    ESP_LOGI(TAG, "scaffold ready (T-01); task + queue arrive in T-03");
    return ESP_OK;
}
