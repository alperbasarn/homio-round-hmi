#include "NVSManager.h"
#include "esp_log.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>

static const char *TAG = "NVSManager";

namespace {

constexpr const char* kDeviceNamePrefix = "Homio-";
constexpr const char* kDefaultDeviceSuffix = "0000";
constexpr const char* kLegacyDeviceName = "ESP32Device";
constexpr const char* kLegacyApPassword = "knobsetup";
constexpr size_t kDeviceSuffixLength = 4;

}  // namespace

NVSManager::NVSManager()
    : lastConnectedNetworkIndex(-1),
      soundTCPServerPort(12345), lightTCPServerPort(12345),
      soundMQTTServerPort(8883), lightMQTTServerPort(8883),
      weatherApiToken(""), weatherCity("Istanbul"), weatherCountryCode("tr"), timeApiToken(""),
      deviceName(std::string(kDeviceNamePrefix) + kDefaultDeviceSuffix),
      accessPointPassword(std::string(kDeviceNamePrefix) + kDefaultDeviceSuffix),
    otaVariantId(""),
    otaManifestUrl(""),
      staticIPEnabled(false),
    staticIPSSID(""),
      staticIP("192.168.4.1"), staticGateway("192.168.4.1"),
      staticSubnet("255.255.255.0"), staticDNS1("8.8.8.8"), staticDNS2("8.8.4.4"),
      nvs_handle(0), initialized(false)
{
}

std::string NVSManager::normalizeDeviceSuffix(const std::string& value)
{
    std::string normalized;
    normalized.reserve(kDeviceSuffixLength);

    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            if (normalized.size() == kDeviceSuffixLength) {
                break;
            }
        }
    }

    if (normalized.empty()) {
        normalized = kDefaultDeviceSuffix;
    }

    while (normalized.size() < kDeviceSuffixLength) {
        normalized.push_back('0');
    }

    return normalized;
}

std::string NVSManager::extractDeviceSuffix(const std::string& value)
{
    if (value.rfind(kDeviceNamePrefix, 0) == 0) {
        return normalizeDeviceSuffix(value.substr(std::strlen(kDeviceNamePrefix)));
    }
    return normalizeDeviceSuffix(value);
}

std::string NVSManager::formatDeviceName(const std::string& value)
{
    return std::string(kDeviceNamePrefix) + extractDeviceSuffix(value);
}

bool NVSManager::isValidAccessPointPassword(const std::string& value)
{
    return value.size() >= 8 && value.size() <= 63;
}

NVSManager::~NVSManager()
{
    if (initialized) {
        nvs_close(nvs_handle);
    }
}

esp_err_t NVSManager::begin()
{
    ESP_LOGI(TAG, "Initializing NVS...");

    // Initialize NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Open NVS namespace
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }

    initialized = true;
    ESP_LOGI(TAG, "NVS initialized successfully");

    return loadAllConfigurations();
}

esp_err_t NVSManager::writeString(const char* key, const std::string& value)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = nvs_set_str(nvs_handle, key, value.c_str());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write string '%s': %s", key, esp_err_to_name(ret));
        return ret;
    }
    return nvs_commit(nvs_handle);
}

esp_err_t NVSManager::readString(const char* key, std::string& value, const std::string& default_value)
{
    if (!initialized) {
        value = default_value;
        return ESP_ERR_INVALID_STATE;
    }

    size_t required_size = 0;
    esp_err_t ret = nvs_get_str(nvs_handle, key, NULL, &required_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        value = default_value;
        return ESP_OK;
    } else if (ret != ESP_OK) {
        value = default_value;
        return ret;
    }

    char* buffer = new char[required_size];
    ret = nvs_get_str(nvs_handle, key, buffer, &required_size);

    if (ret == ESP_OK) {
        value = buffer;
    } else {
        value = default_value;
    }

    delete[] buffer;
    return ret;
}

esp_err_t NVSManager::writeInt(const char* key, int32_t value)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = nvs_set_i32(nvs_handle, key, value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write int '%s': %s", key, esp_err_to_name(ret));
        return ret;
    }
    return nvs_commit(nvs_handle);
}

esp_err_t NVSManager::readInt(const char* key, int32_t& value, int32_t default_value)
{
    if (!initialized) {
        value = default_value;
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = nvs_get_i32(nvs_handle, key, &value);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        value = default_value;
        return ESP_OK;
    }
    return ret;
}

esp_err_t NVSManager::writeBool(const char* key, bool value)
{
    return writeInt(key, value ? 1 : 0);
}

esp_err_t NVSManager::readBool(const char* key, bool& value, bool default_value)
{
    int32_t int_value;
    esp_err_t ret = readInt(key, int_value, default_value ? 1 : 0);
    value = (int_value != 0);
    return ret;
}

esp_err_t NVSManager::saveWiFiCredentials()
{
    char key[32];

    for (int i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        snprintf(key, sizeof(key), NVS_KEY_WIFI_SSID, i);
        writeString(key, wifiCredentials[i].ssid);

        snprintf(key, sizeof(key), NVS_KEY_WIFI_PASS, i);
        writeString(key, wifiCredentials[i].password);

        snprintf(key, sizeof(key), NVS_KEY_WIFI_REMEMBER, i);
        writeBool(key, wifiCredentials[i].remember);
    }

    writeInt(NVS_KEY_LAST_WIFI_IDX, lastConnectedNetworkIndex);

    ESP_LOGI(TAG, "WiFi credentials saved");
    return ESP_OK;
}

esp_err_t NVSManager::loadWiFiCredentials()
{
    char key[32];

    for (int i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        snprintf(key, sizeof(key), NVS_KEY_WIFI_SSID, i);
        readString(key, wifiCredentials[i].ssid, "");

        snprintf(key, sizeof(key), NVS_KEY_WIFI_PASS, i);
        readString(key, wifiCredentials[i].password, "");

        snprintf(key, sizeof(key), NVS_KEY_WIFI_REMEMBER, i);
        readBool(key, wifiCredentials[i].remember, false);
    }

    int32_t idx;
    readInt(NVS_KEY_LAST_WIFI_IDX, idx, -1);
    lastConnectedNetworkIndex = idx;

    ESP_LOGI(TAG, "WiFi credentials loaded");
    return ESP_OK;
}

esp_err_t NVSManager::clearAll()
{
    if (!initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = nvs_erase_all(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS clear: %s", esp_err_to_name(ret));
        return ret;
    }

    // Reset to defaults
    for (int i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        wifiCredentials[i].ssid = "";
        wifiCredentials[i].password = "";
        wifiCredentials[i].remember = false;
    }
    lastConnectedNetworkIndex = -1;

    soundTCPServerIP = "";
    soundTCPServerPort = 12345;
    lightTCPServerIP = "";
    lightTCPServerPort = 12345;

    soundMQTTServerURL = "";
    soundMQTTServerPort = 8883;
    soundMQTTUsername = "";
    soundMQTTPassword = "";
    lightMQTTServerURL = "";
    lightMQTTServerPort = 8883;
    lightMQTTUsername = "";
    lightMQTTPassword = "";

    weatherApiToken = "";
    weatherCity = "Istanbul";
    weatherCountryCode = "tr";
    timeApiToken = "";

    deviceName = std::string(kDeviceNamePrefix) + kDefaultDeviceSuffix;
    accessPointPassword = deviceName;
    otaVariantId.clear();
    otaManifestUrl.clear();

    staticIPEnabled = false;
    staticIPSSID.clear();
    staticIP = "192.168.4.1";
    staticGateway = "192.168.4.1";
    staticSubnet = "255.255.255.0";
    staticDNS1 = "8.8.8.8";
    staticDNS2 = "8.8.4.4";

    ESP_LOGI(TAG, "NVS cleared and reset to defaults");
    return ESP_OK;
}

esp_err_t NVSManager::saveSoundTCPServer(const std::string& ip, int port)
{
    soundTCPServerIP = ip;
    soundTCPServerPort = port;
    writeString(NVS_KEY_SOUND_TCP_IP, ip);
    writeInt(NVS_KEY_SOUND_TCP_PORT, port);
    ESP_LOGI(TAG, "Sound TCP server saved: %s:%d", ip.c_str(), port);
    return ESP_OK;
}

esp_err_t NVSManager::saveLightTCPServer(const std::string& ip, int port)
{
    lightTCPServerIP = ip;
    lightTCPServerPort = port;
    writeString(NVS_KEY_LIGHT_TCP_IP, ip);
    writeInt(NVS_KEY_LIGHT_TCP_PORT, port);
    ESP_LOGI(TAG, "Light TCP server saved: %s:%d", ip.c_str(), port);
    return ESP_OK;
}

esp_err_t NVSManager::saveSoundMQTTServer(const std::string& url, int port,
                                          const std::string& username, const std::string& password)
{
    soundMQTTServerURL = url;
    soundMQTTServerPort = port;
    soundMQTTUsername = username;
    soundMQTTPassword = password;

    writeString(NVS_KEY_SOUND_MQTT_URL, url);
    writeInt(NVS_KEY_SOUND_MQTT_PORT, port);
    writeString(NVS_KEY_SOUND_MQTT_USER, username);
    writeString(NVS_KEY_SOUND_MQTT_PASS, password);

    ESP_LOGI(TAG, "Sound MQTT server saved: %s:%d", url.c_str(), port);
    return ESP_OK;
}

esp_err_t NVSManager::saveLightMQTTServer(const std::string& url, int port,
                                          const std::string& username, const std::string& password)
{
    lightMQTTServerURL = url;
    lightMQTTServerPort = port;
    lightMQTTUsername = username;
    lightMQTTPassword = password;

    writeString(NVS_KEY_LIGHT_MQTT_URL, url);
    writeInt(NVS_KEY_LIGHT_MQTT_PORT, port);
    writeString(NVS_KEY_LIGHT_MQTT_USER, username);
    writeString(NVS_KEY_LIGHT_MQTT_PASS, password);

    ESP_LOGI(TAG, "Light MQTT server saved: %s:%d", url.c_str(), port);
    return ESP_OK;
}

esp_err_t NVSManager::saveWeatherConfig(const std::string& apiToken,
                                        const std::string& city,
                                        const std::string& countryCode)
{
    weatherApiToken = apiToken;
    weatherCity = city;
    weatherCountryCode = countryCode;

    writeString(NVS_KEY_WEATHER_API_TOKEN, weatherApiToken);
    writeString(NVS_KEY_WEATHER_CITY, weatherCity);
    writeString(NVS_KEY_WEATHER_COUNTRY, weatherCountryCode);

    ESP_LOGI(TAG, "Weather config saved: city=%s, country=%s", weatherCity.c_str(), weatherCountryCode.c_str());
    return ESP_OK;
}

esp_err_t NVSManager::saveTimeApiToken(const std::string& apiToken)
{
    timeApiToken = apiToken;
    writeString(NVS_KEY_TIME_API_TOKEN, timeApiToken);
    ESP_LOGI(TAG, "Time API token saved");
    return ESP_OK;
}

esp_err_t NVSManager::saveDeviceName(const std::string& name)
{
    deviceName = formatDeviceName(name);
    accessPointPassword = deviceName;
    writeString(NVS_KEY_DEVICE_NAME, deviceName);
    writeString(NVS_KEY_AP_PASSWORD, accessPointPassword);
    ESP_LOGI(TAG, "Device name saved: %s", deviceName.c_str());
    return ESP_OK;
}

esp_err_t NVSManager::saveAccessPointPassword(const std::string& password)
{
    const std::string nextPassword = password.empty() ? deviceName : password;
    if (!isValidAccessPointPassword(nextPassword)) {
        ESP_LOGW(TAG, "Rejected AP password update due to invalid length: %u",
                 static_cast<unsigned>(nextPassword.size()));
        return ESP_ERR_INVALID_ARG;
    }

    accessPointPassword = nextPassword;
    writeString(NVS_KEY_AP_PASSWORD, accessPointPassword);
    ESP_LOGI(TAG, "Access point password saved");
    return ESP_OK;
}

esp_err_t NVSManager::saveOtaConfig(const std::string& variantId, const std::string& manifestUrl)
{
    otaVariantId = variantId;
    otaManifestUrl = manifestUrl;
    writeString(NVS_KEY_OTA_VARIANT, otaVariantId);
    writeString(NVS_KEY_OTA_MANIFEST_URL, otaManifestUrl);
    ESP_LOGI(TAG, "OTA config saved: variant=%s, manifest=%s",
             otaVariantId.c_str(), otaManifestUrl.c_str());
    return ESP_OK;
}

esp_err_t NVSManager::saveStaticIPConfig(bool enabled, const std::string& ip, const std::string& gateway,
                                          const std::string& subnet, const std::string& dns1, const std::string& dns2,
                                          const std::string& ssid)
{
    staticIPEnabled = enabled;
    staticIPSSID = ssid;
    staticIP = ip;
    staticGateway = gateway;
    staticSubnet = subnet;
    staticDNS1 = dns1;
    staticDNS2 = dns2;

    writeBool(NVS_KEY_STATIC_ENABLED, enabled);
    writeString(NVS_KEY_STATIC_IP, ip);
    writeString(NVS_KEY_STATIC_GATEWAY, gateway);
    writeString(NVS_KEY_STATIC_SUBNET, subnet);
    writeString(NVS_KEY_STATIC_DNS1, dns1);
    writeString(NVS_KEY_STATIC_DNS2, dns2);

    ESP_LOGI(TAG, "Static IP config saved: enabled=%d, ip=%s", enabled, ip.c_str());
    return ESP_OK;
}

esp_err_t NVSManager::loadStaticIPConfig()
{
    readBool(NVS_KEY_STATIC_ENABLED, staticIPEnabled, false);
    readString(NVS_KEY_STATIC_SSID, staticIPSSID, "");
    readString(NVS_KEY_STATIC_IP, staticIP, "192.168.4.1");
    readString(NVS_KEY_STATIC_GATEWAY, staticGateway, "192.168.4.1");
    readString(NVS_KEY_STATIC_SUBNET, staticSubnet, "255.255.255.0");
    readString(NVS_KEY_STATIC_DNS1, staticDNS1, "8.8.8.8");
    readString(NVS_KEY_STATIC_DNS2, staticDNS2, "8.8.4.4");

    ESP_LOGI(TAG, "Static IP config loaded: enabled=%d", staticIPEnabled);
    return ESP_OK;
}

esp_err_t NVSManager::loadAllConfigurations()
{
    ESP_LOGI(TAG, "Loading all configurations...");

    loadWiFiCredentials();

    // TCP servers
    int32_t port;
    readString(NVS_KEY_SOUND_TCP_IP, soundTCPServerIP, "");
    readInt(NVS_KEY_SOUND_TCP_PORT, port, 12345);
    soundTCPServerPort = port;

    readString(NVS_KEY_LIGHT_TCP_IP, lightTCPServerIP, "");
    readInt(NVS_KEY_LIGHT_TCP_PORT, port, 12345);
    lightTCPServerPort = port;

    // MQTT servers
    readString(NVS_KEY_SOUND_MQTT_URL, soundMQTTServerURL, "");
    readInt(NVS_KEY_SOUND_MQTT_PORT, port, 8883);
    soundMQTTServerPort = port;
    readString(NVS_KEY_SOUND_MQTT_USER, soundMQTTUsername, "");
    readString(NVS_KEY_SOUND_MQTT_PASS, soundMQTTPassword, "");

    readString(NVS_KEY_LIGHT_MQTT_URL, lightMQTTServerURL, "");
    readInt(NVS_KEY_LIGHT_MQTT_PORT, port, 8883);
    lightMQTTServerPort = port;
    readString(NVS_KEY_LIGHT_MQTT_USER, lightMQTTUsername, "");
    readString(NVS_KEY_LIGHT_MQTT_PASS, lightMQTTPassword, "");

    // Weather and time settings
    readString(NVS_KEY_WEATHER_API_TOKEN, weatherApiToken, "");
    readString(NVS_KEY_WEATHER_CITY, weatherCity, "Istanbul");
    readString(NVS_KEY_WEATHER_COUNTRY, weatherCountryCode, "tr");
    readString(NVS_KEY_TIME_API_TOKEN, timeApiToken, "");

    // Device name / AP credentials
    readString(NVS_KEY_DEVICE_NAME, deviceName, std::string(kDeviceNamePrefix) + kDefaultDeviceSuffix);
    if (deviceName.empty() || deviceName == kLegacyDeviceName) {
        deviceName = std::string(kDeviceNamePrefix) + kDefaultDeviceSuffix;
        writeString(NVS_KEY_DEVICE_NAME, deviceName);
    } else {
        const std::string normalizedDeviceName = formatDeviceName(deviceName);
        if (normalizedDeviceName != deviceName) {
            deviceName = normalizedDeviceName;
            writeString(NVS_KEY_DEVICE_NAME, deviceName);
        }
    }

    readString(NVS_KEY_AP_PASSWORD, accessPointPassword, deviceName);
    if (accessPointPassword.empty() || accessPointPassword == kLegacyApPassword) {
        accessPointPassword = deviceName;
        writeString(NVS_KEY_AP_PASSWORD, accessPointPassword);
    }

    // OTA settings
    readString(NVS_KEY_OTA_VARIANT, otaVariantId, "");
    readString(NVS_KEY_OTA_MANIFEST_URL, otaManifestUrl, "");

    // Static IP
    loadStaticIPConfig();

    ESP_LOGI(TAG, "All configurations loaded");
    return ESP_OK;
}

std::string NVSManager::getConfigurationInfo()
{
    std::string info = "=== CONFIGURATION INFO ===\n";

    info += "Device Name: " + deviceName + "\n\n";
    info += "AP Password: ";
    info += accessPointPassword.empty() ? "Not Set" : "Configured";
    info += "\n\n";

    info += "WiFi Networks:\n";
    for (int i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        info += "  Slot " + std::to_string(i) + ": ";
        if (wifiCredentials[i].ssid.empty()) {
            info += "Not Set\n";
        } else {
            info += "SSID: " + wifiCredentials[i].ssid + "\n";
        }
    }
    info += "Last Used: " + std::to_string(lastConnectedNetworkIndex) + "\n\n";

    info += "Sound TCP Server:\n";
    info += "  IP: " + (soundTCPServerIP.empty() ? "Not Set" : soundTCPServerIP) + "\n";
    info += "  Port: " + std::to_string(soundTCPServerPort) + "\n\n";

    info += "Light TCP Server:\n";
    info += "  IP: " + (lightTCPServerIP.empty() ? "Not Set" : lightTCPServerIP) + "\n";
    info += "  Port: " + std::to_string(lightTCPServerPort) + "\n\n";

    info += "Sound MQTT Server:\n";
    info += "  URL: " + (soundMQTTServerURL.empty() ? "Not Set" : soundMQTTServerURL) + "\n";
    info += "  Port: " + std::to_string(soundMQTTServerPort) + "\n";
    info += "  Username: " + (soundMQTTUsername.empty() ? "Not Set" : soundMQTTUsername) + "\n\n";

    info += "Light MQTT Server:\n";
    info += "  URL: " + (lightMQTTServerURL.empty() ? "Not Set" : lightMQTTServerURL) + "\n";
    info += "  Port: " + std::to_string(lightMQTTServerPort) + "\n";
    info += "  Username: " + (lightMQTTUsername.empty() ? "Not Set" : lightMQTTUsername) + "\n\n";

    info += "Weather:\n";
    info += "  City: " + weatherCity + "\n";
    info += "  Country: " + weatherCountryCode + "\n";
    info += "  API Token: ";
    info += (weatherApiToken.empty() ? "Not Set" : "Configured");
    info += "\n\n";

    info += "Time API Token: ";
    info += (timeApiToken.empty() ? "Not Set" : "Configured");
    info += "\n\n";

    info += getStaticIPInfo();

    return info;
}

std::string NVSManager::getStaticIPInfo()
{
    std::string info = "Static IP Configuration:\n";
    info += "  Enabled: " + std::string(staticIPEnabled ? "Yes" : "No") + "\n";
    info += "  IP: " + staticIP + "\n";
    info += "  Gateway: " + staticGateway + "\n";
    info += "  Subnet: " + staticSubnet + "\n";
    info += "  DNS1: " + staticDNS1 + "\n";
    info += "  DNS2: " + staticDNS2 + "\n";
    return info;
}

// C-compatible interface
extern "C" {

static std::string g_device_name_buffer;

nvs_manager_handle_t nvs_manager_create(void)
{
    return new NVSManager();
}

void nvs_manager_destroy(nvs_manager_handle_t handle)
{
    delete static_cast<NVSManager*>(handle);
}

esp_err_t nvs_manager_begin(nvs_manager_handle_t handle)
{
    return static_cast<NVSManager*>(handle)->begin();
}

esp_err_t nvs_manager_load_all(nvs_manager_handle_t handle)
{
    return static_cast<NVSManager*>(handle)->loadAllConfigurations();
}

const char* nvs_manager_get_device_name(nvs_manager_handle_t handle)
{
    g_device_name_buffer = static_cast<NVSManager*>(handle)->deviceName;
    return g_device_name_buffer.c_str();
}

}
