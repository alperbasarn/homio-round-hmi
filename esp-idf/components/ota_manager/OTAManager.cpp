#include "OTAManager.h"

#include <new>

#include "WiFiManager.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char* TAG = "OTAManager";
constexpr uint32_t OTA_HTTP_TIMEOUT_MS = 15000;
constexpr uint32_t OTA_PROGRESS_LOG_MS = 500;
constexpr uint32_t OTA_TASK_STACK_SIZE = 12288;
constexpr UBaseType_t OTA_TASK_PRIORITY = 5;

bool startsWith(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}
}  // namespace

OTAManager::OTAManager(WiFiManager* wifiManagerValue)
    : wifiManager(wifiManagerValue),
      serverCertPem(nullptr),
      allowInsecure(false),
      busy(false),
      lastError(ESP_OK) {
}

OTAManager::~OTAManager() = default;

void OTAManager::setServerCert(const char* certPem) {
    serverCertPem = certPem;
}

void OTAManager::setAllowInsecure(bool allow) {
    allowInsecure = allow;
}

bool OTAManager::isNetworkReady() const {
    return wifiManager != nullptr && wifiManager->isConnected();
}

esp_err_t OTAManager::startUpdate(const std::string& url, bool rebootOnSuccess) {
    if (busy.load()) {
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    if (url.empty()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!isNetworkReady()) {
        ESP_LOGE(TAG, "WiFi not connected - cannot start OTA");
        return ESP_ERR_INVALID_STATE;
    }

#if !CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP
    if (startsWith(url, "http://")) {
        ESP_LOGE(TAG, "HTTP OTA is disabled. Enable CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP");
        return ESP_ERR_INVALID_STATE;
    }
#endif

    lastError = ESP_OK;
    busy.store(true);

    auto* args = new (std::nothrow) OtaTaskArgs{this, url, rebootOnSuccess};
    if (args == nullptr) {
        busy.store(false);
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(otaTaskEntry, "OTATask", OTA_TASK_STACK_SIZE, args, OTA_TASK_PRIORITY, nullptr) != pdPASS) {
        delete args;
        busy.store(false);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void OTAManager::otaTaskEntry(void* arg) {
    auto* args = static_cast<OtaTaskArgs*>(arg);
    if (args == nullptr || args->self == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    args->self->otaTask(args->url, args->reboot);
    delete args;
    vTaskDelete(nullptr);
}

void OTAManager::otaTask(const std::string& url, bool reboot) {
    ESP_LOGI(TAG, "Starting OTA update from %s", url.c_str());

    esp_http_client_config_t http_config = {};
    http_config.url = url.c_str();
    http_config.timeout_ms = OTA_HTTP_TIMEOUT_MS;
    http_config.keep_alive_enable = true;
    http_config.skip_cert_common_name_check = allowInsecure;

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    if (serverCertPem != nullptr) {
        http_config.cert_pem = serverCertPem;
    }

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &http_config;

    esp_https_ota_handle_t ota_handle = nullptr;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        lastError = err;
        busy.store(false);
        return;
    }

    esp_app_desc_t new_app_info = {};
    if (esp_https_ota_get_img_desc(ota_handle, &new_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);
    }

    int64_t last_log = 0;
    while (true) {
        err = esp_https_ota_perform(ota_handle);
        if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now - last_log >= OTA_PROGRESS_LOG_MS) {
                int64_t downloaded = esp_https_ota_get_image_len_read(ota_handle);
                ESP_LOGI(TAG, "OTA progress: %lld bytes", static_cast<long long>(downloaded));
                last_log = now;
            }
            continue;
        }
        break;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        lastError = err;
        busy.store(false);
        return;
    }

    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        ESP_LOGE(TAG, "OTA data not fully received");
        esp_https_ota_abort(ota_handle);
        lastError = ESP_FAIL;
        busy.store(false);
        return;
    }

    err = esp_https_ota_finish(ota_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful");
        lastError = ESP_OK;
        busy.store(false);
        if (reboot) {
            ESP_LOGI(TAG, "Rebooting...");
            esp_restart();
        }
        return;
    }

    ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
    lastError = err;
    busy.store(false);
}

std::string OTAManager::getRunningVersion() const {
    const esp_app_desc_t* desc = esp_app_get_description();
    if (desc == nullptr) {
        return "unknown";
    }
    return desc->version;
}

std::string OTAManager::getRunningPartitionLabel() const {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) {
        return "unknown";
    }
    return running->label;
}
