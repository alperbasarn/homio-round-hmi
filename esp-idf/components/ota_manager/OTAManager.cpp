#include "OTAManager.h"

#include <new>
#include <vector>

#include "WiFiManager.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
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
constexpr uint32_t OTA_RELEASE_TASK_STACK_SIZE = 10240;
constexpr UBaseType_t OTA_TASK_PRIORITY = 5;

std::vector<int> parseVersionParts(const std::string& version) {
    std::vector<int> parts;
    size_t start = 0;
    while (start < version.size()) {
        size_t end = version.find('.', start);
        const std::string token = version.substr(start, end == std::string::npos ? std::string::npos : end - start);
        char* tail = nullptr;
        long value = std::strtol(token.c_str(), &tail, 10);
        if (tail == nullptr || *tail != '\0') {
            return {};
        }
        parts.push_back(static_cast<int>(value));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

int compareVersions(const std::string& left, const std::string& right) {
    const std::vector<int> leftParts = parseVersionParts(left);
    const std::vector<int> rightParts = parseVersionParts(right);
    if (leftParts.empty() || rightParts.empty()) {
        if (left == right) {
            return 0;
        }
        return left < right ? -1 : 1;
    }

    const size_t count = std::max(leftParts.size(), rightParts.size());
    for (size_t index = 0; index < count; ++index) {
        const int leftValue = index < leftParts.size() ? leftParts[index] : 0;
        const int rightValue = index < rightParts.size() ? rightParts[index] : 0;
        if (leftValue < rightValue) {
            return -1;
        }
        if (leftValue > rightValue) {
            return 1;
        }
    }
    return 0;
}
}  // namespace

OTAManager::OTAManager(WiFiManager* wifiManagerValue)
    : wifiManager(wifiManagerValue),
      serverCertPem(nullptr),
      allowInsecure(false),
      busy(false),
      lastError(ESP_OK),
      updateAvailable(false),
      stateMutex(xSemaphoreCreateMutex()) {
}

OTAManager::~OTAManager() {
    if (stateMutex != nullptr) {
        vSemaphoreDelete(stateMutex);
        stateMutex = nullptr;
    }
}

void OTAManager::setServerCert(const char* certPem) {
    serverCertPem = certPem;
}

void OTAManager::setAllowInsecure(bool allow) {
    allowInsecure = allow;
}

void OTAManager::setManifestUrl(const std::string& url) {
    if (stateMutex != nullptr) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        manifestUrl = url;
        xSemaphoreGive(stateMutex);
    } else {
        manifestUrl = url;
    }
}

void OTAManager::setDeviceVariantId(const std::string& variantId) {
    if (stateMutex != nullptr) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        deviceVariantId = variantId;
        xSemaphoreGive(stateMutex);
    } else {
        deviceVariantId = variantId;
    }
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
    if (url.rfind("http://", 0) == 0) {
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

esp_err_t OTAManager::startReleaseUpdate(bool rebootOnSuccess) {
    if (busy.load()) {
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    if (!isNetworkReady()) {
        ESP_LOGE(TAG, "WiFi not connected - cannot start OTA release update");
        return ESP_ERR_INVALID_STATE;
    }

    std::string manifestUrlCopy;
    if (stateMutex != nullptr) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        manifestUrlCopy = manifestUrl;
        xSemaphoreGive(stateMutex);
    } else {
        manifestUrlCopy = manifestUrl;
    }
    if (manifestUrlCopy.empty()) {
        return ESP_ERR_INVALID_STATE;
    }

    lastError = ESP_OK;
    busy.store(true);
    updateReleaseState(availableVersion, "Checking for software update...", false, ESP_OK);

    auto* args = new (std::nothrow) ReleaseTaskArgs{this, rebootOnSuccess};
    if (args == nullptr) {
        busy.store(false);
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(releaseTaskEntry, "OTARelease", OTA_RELEASE_TASK_STACK_SIZE, args, OTA_TASK_PRIORITY, nullptr) != pdPASS) {
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

void OTAManager::releaseTaskEntry(void* arg) {
    auto* args = static_cast<ReleaseTaskArgs*>(arg);
    if (args == nullptr || args->self == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    args->self->releaseTask(args->reboot);
    delete args;
    vTaskDelete(nullptr);
}

void OTAManager::otaTask(const std::string& url, bool reboot) {
    performOta(url, reboot);
}

void OTAManager::releaseTask(bool reboot) {
    std::string nextVersion;
    std::string downloadUrl;
    esp_err_t err = fetchManifest(nextVersion, downloadUrl);
    if (err != ESP_OK) {
        lastError = err;
        busy.store(false);
        return;
    }

    const std::string currentVersion = getRunningVersion();
    if (compareVersions(currentVersion, nextVersion) >= 0) {
        updateReleaseState(nextVersion, "Software is already up to date", false, ESP_OK);
        lastError = ESP_OK;
        busy.store(false);
        return;
    }

    updateReleaseState(nextVersion, std::string("Installing ") + nextVersion + "...", true, ESP_OK);
    performOta(downloadUrl, reboot);
}

esp_err_t OTAManager::performOta(const std::string& url, bool reboot) {
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
        updateReleaseState(availableVersion, "Failed to start download", updateAvailable, err);
        lastError = err;
        busy.store(false);
        return err;
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
                updateReleaseState(availableVersion,
                                   std::string("Downloading update (") + std::to_string(static_cast<long long>(downloaded)) + " bytes)...",
                                   updateAvailable,
                                   ESP_OK);
                last_log = now;
            }
            continue;
        }
        break;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        updateReleaseState(availableVersion, "Download failed", updateAvailable, err);
        lastError = err;
        busy.store(false);
        return err;
    }

    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        ESP_LOGE(TAG, "OTA data not fully received");
        esp_https_ota_abort(ota_handle);
        updateReleaseState(availableVersion, "Update payload incomplete", updateAvailable, ESP_FAIL);
        lastError = ESP_FAIL;
        busy.store(false);
        return ESP_FAIL;
    }

    err = esp_https_ota_finish(ota_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful");
        updateReleaseState(availableVersion, "Update installed successfully", false, ESP_OK);
        lastError = ESP_OK;
        busy.store(false);
        if (reboot) {
            ESP_LOGI(TAG, "Rebooting...");
            esp_restart();
        }
        return ESP_OK;
    }

    ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
    updateReleaseState(availableVersion, "Failed to finalize update", updateAvailable, err);
    lastError = err;
    busy.store(false);
    return err;
}

esp_err_t OTAManager::fetchManifest(std::string& outVersion, std::string& outUrl) {
    std::string manifestUrlCopy;
    std::string variantIdCopy;
    if (stateMutex != nullptr) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        manifestUrlCopy = manifestUrl;
        variantIdCopy = deviceVariantId;
        xSemaphoreGive(stateMutex);
    } else {
        manifestUrlCopy = manifestUrl;
        variantIdCopy = deviceVariantId;
    }

    if (manifestUrlCopy.empty()) {
        updateReleaseState(availableVersion, "OTA manifest URL not configured", false, ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    esp_http_client_config_t httpConfig = {};
    httpConfig.url = manifestUrlCopy.c_str();
    httpConfig.timeout_ms = OTA_HTTP_TIMEOUT_MS;
    httpConfig.keep_alive_enable = true;
    httpConfig.skip_cert_common_name_check = allowInsecure;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    httpConfig.crt_bundle_attach = esp_crt_bundle_attach;
#endif
    if (serverCertPem != nullptr) {
        httpConfig.cert_pem = serverCertPem;
    }

    esp_http_client_handle_t client = esp_http_client_init(&httpConfig);
    if (client == nullptr) {
        updateReleaseState(availableVersion, "Failed to initialize manifest request", false, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        updateReleaseState(availableVersion, "Failed to fetch update manifest", false, err);
        return err;
    }

    const int statusCode = esp_http_client_fetch_headers(client);
    if (statusCode < 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        updateReleaseState(availableVersion, "Manifest response headers invalid", false, ESP_FAIL);
        return ESP_FAIL;
    }

    std::string payload;
    char buffer[512];
    while (true) {
        const int bytesRead = esp_http_client_read(client, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            updateReleaseState(availableVersion, "Manifest download failed", false, ESP_FAIL);
            return ESP_FAIL;
        }
        if (bytesRead == 0) {
            break;
        }
        payload.append(buffer, static_cast<size_t>(bytesRead));
    }

    const int httpStatusCode = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (httpStatusCode != 200) {
        updateReleaseState(availableVersion,
                           std::string("Manifest fetch returned HTTP ") + std::to_string(httpStatusCode),
                           false,
                           ESP_FAIL);
        return ESP_FAIL;
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    if (root == nullptr) {
        updateReleaseState(availableVersion, "Manifest JSON is invalid", false, ESP_ERR_INVALID_RESPONSE);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON* variant = cJSON_GetObjectItemCaseSensitive(root, "variant");
    const cJSON* binaryUrl = cJSON_GetObjectItemCaseSensitive(root, "binary_url");

    if (!cJSON_IsString(version) || !cJSON_IsString(binaryUrl) ||
        (variant != nullptr && !cJSON_IsString(variant))) {
        cJSON_Delete(root);
        updateReleaseState(availableVersion, "Manifest is missing required fields", false, ESP_ERR_INVALID_RESPONSE);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!variantIdCopy.empty() && variant != nullptr && variant->valuestring != nullptr && variantIdCopy != variant->valuestring) {
        cJSON_Delete(root);
        updateReleaseState(availableVersion, "Manifest variant does not match device", false, ESP_ERR_INVALID_RESPONSE);
        return ESP_ERR_INVALID_RESPONSE;
    }

    outVersion = version->valuestring;
    outUrl = binaryUrl->valuestring;
    cJSON_Delete(root);
    return ESP_OK;
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

OtaReleaseInfo OTAManager::getReleaseInfo() const {
    OtaReleaseInfo info;
    info.busy = busy.load();
    info.currentVersion = getRunningVersion();
    info.lastError = lastError;

    if (stateMutex != nullptr) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        info.configured = !manifestUrl.empty();
        info.updateAvailable = updateAvailable;
        info.availableVersion = availableVersion;
        info.statusMessage = statusMessage;
        info.manifestUrl = manifestUrl;
        xSemaphoreGive(stateMutex);
    } else {
        info.configured = !manifestUrl.empty();
        info.updateAvailable = updateAvailable;
        info.availableVersion = availableVersion;
        info.statusMessage = statusMessage;
        info.manifestUrl = manifestUrl;
    }

    if (info.statusMessage.empty()) {
        info.statusMessage = info.configured ? "Ready to check for updates" : "OTA is not configured";
    }
    return info;
}

void OTAManager::updateReleaseState(const std::string& nextAvailableVersion,
                                    const std::string& nextStatusMessage,
                                    bool nextUpdateAvailable,
                                    esp_err_t nextError) {
    if (stateMutex != nullptr) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        availableVersion = nextAvailableVersion;
        statusMessage = nextStatusMessage;
        updateAvailable = nextUpdateAvailable;
        lastError = nextError;
        xSemaphoreGive(stateMutex);
        return;
    }

    availableVersion = nextAvailableVersion;
    statusMessage = nextStatusMessage;
    updateAvailable = nextUpdateAvailable;
    lastError = nextError;
}
