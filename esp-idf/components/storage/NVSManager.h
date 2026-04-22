#pragma once

#include <string>
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"

// Configuration limits
#define NUM_WIFI_CREDENTIALS    3
#define MAX_SSID_LENGTH         32
#define MAX_PASS_LENGTH         64
#define MAX_IP_LENGTH           16
#define MAX_URL_LENGTH          64
#define MAX_USERNAME_LENGTH     32
#define MAX_DEVICE_NAME_LENGTH  32

// NVS Keys
#define NVS_NAMESPACE           "qnob_config"

// WiFi credential keys
#define NVS_KEY_WIFI_SSID       "wifi_ssid_%d"
#define NVS_KEY_WIFI_PASS       "wifi_pass_%d"
#define NVS_KEY_WIFI_REMEMBER   "wifi_rem_%d"
#define NVS_KEY_LAST_WIFI_IDX   "last_wifi_idx"

// TCP server keys
#define NVS_KEY_SOUND_TCP_IP    "snd_tcp_ip"
#define NVS_KEY_SOUND_TCP_PORT  "snd_tcp_port"
#define NVS_KEY_LIGHT_TCP_IP    "lgt_tcp_ip"
#define NVS_KEY_LIGHT_TCP_PORT  "lgt_tcp_port"

// MQTT server keys
#define NVS_KEY_SOUND_MQTT_URL  "snd_mqtt_url"
#define NVS_KEY_SOUND_MQTT_PORT "snd_mqtt_port"
#define NVS_KEY_SOUND_MQTT_USER "snd_mqtt_user"
#define NVS_KEY_SOUND_MQTT_PASS "snd_mqtt_pass"
#define NVS_KEY_LIGHT_MQTT_URL  "lgt_mqtt_url"
#define NVS_KEY_LIGHT_MQTT_PORT "lgt_mqtt_port"
#define NVS_KEY_LIGHT_MQTT_USER "lgt_mqtt_user"
#define NVS_KEY_LIGHT_MQTT_PASS "lgt_mqtt_pass"

// Weather and time keys
#define NVS_KEY_WEATHER_API_TOKEN "wth_api_tok"
#define NVS_KEY_WEATHER_CITY      "wth_city"
#define NVS_KEY_WEATHER_COUNTRY   "wth_country"
#define NVS_KEY_TIME_API_TOKEN    "time_api_tok"

// OTA keys
#define NVS_KEY_OTA_VARIANT      "ota_variant"
#define NVS_KEY_OTA_MANIFEST_URL "ota_manifest"

// Device keys
#define NVS_KEY_DEVICE_NAME     "device_name"
#define NVS_KEY_AP_PASSWORD     "ap_password"

// Static IP keys
#define NVS_KEY_STATIC_ENABLED  "static_en"
#define NVS_KEY_STATIC_SSID     "static_ssid"
#define NVS_KEY_STATIC_IP       "static_ip"
#define NVS_KEY_STATIC_GATEWAY  "static_gw"
#define NVS_KEY_STATIC_SUBNET   "static_sub"
#define NVS_KEY_STATIC_DNS1     "static_dns1"
#define NVS_KEY_STATIC_DNS2     "static_dns2"

#ifdef __cplusplus

struct WifiCredential {
    std::string ssid;
    std::string password;
    bool remember;

    WifiCredential() : ssid(""), password(""), remember(false) {}
};

class NVSManager {
public:
    // WiFi credentials
    WifiCredential wifiCredentials[NUM_WIFI_CREDENTIALS];
    int lastConnectedNetworkIndex;

    // TCP Server configurations
    std::string soundTCPServerIP;
    int soundTCPServerPort;
    std::string lightTCPServerIP;
    int lightTCPServerPort;

    // MQTT Server configurations
    std::string soundMQTTServerURL;
    int soundMQTTServerPort;
    std::string soundMQTTUsername;
    std::string soundMQTTPassword;

    std::string lightMQTTServerURL;
    int lightMQTTServerPort;
    std::string lightMQTTUsername;
    std::string lightMQTTPassword;

    // Weather/time settings
    std::string weatherApiToken;
    std::string weatherCity;
    std::string weatherCountryCode;
    std::string timeApiToken;

    // Device name
    std::string deviceName;
    std::string accessPointPassword;

    // OTA settings
    std::string otaVariantId;
    std::string otaManifestUrl;

    // Static IP Configuration
    bool staticIPEnabled;
    std::string staticIPSSID;
    std::string staticIP;
    std::string staticGateway;
    std::string staticSubnet;
    std::string staticDNS1;
    std::string staticDNS2;

    NVSManager();
    ~NVSManager();

    esp_err_t begin();
    esp_err_t saveWiFiCredentials();
    esp_err_t loadWiFiCredentials();
    esp_err_t clearAll();

    // TCP Server configuration
    esp_err_t saveSoundTCPServer(const std::string& ip, int port);
    esp_err_t saveLightTCPServer(const std::string& ip, int port);

    // MQTT Server configuration
    esp_err_t saveSoundMQTTServer(const std::string& url, int port,
                                   const std::string& username, const std::string& password);
    esp_err_t saveLightMQTTServer(const std::string& url, int port,
                                   const std::string& username, const std::string& password);

    // Weather/time configuration
    esp_err_t saveWeatherConfig(const std::string& apiToken,
                                const std::string& city,
                                const std::string& countryCode);
    esp_err_t saveTimeApiToken(const std::string& apiToken);

    // Device name
    esp_err_t saveDeviceName(const std::string& name);
    esp_err_t saveAccessPointPassword(const std::string& password);
    esp_err_t saveOtaConfig(const std::string& variantId, const std::string& manifestUrl);

    static std::string normalizeDeviceSuffix(const std::string& value);
    static std::string formatDeviceName(const std::string& value);
    static std::string extractDeviceSuffix(const std::string& value);
    static bool isValidAccessPointPassword(const std::string& value);

    // Static IP configuration
    esp_err_t saveStaticIPConfig(bool enabled, const std::string& ip, const std::string& gateway,
                                  const std::string& subnet, const std::string& dns1, const std::string& dns2,
                                  const std::string& ssid = "");
    esp_err_t loadStaticIPConfig();

    // Load all configurations
    esp_err_t loadAllConfigurations();

    // Get configuration info as string
    std::string getConfigurationInfo();
    std::string getStaticIPInfo();

private:
    nvs_handle_t nvs_handle;
    bool initialized;

    esp_err_t writeString(const char* key, const std::string& value);
    esp_err_t readString(const char* key, std::string& value, const std::string& default_value = "");
    esp_err_t writeInt(const char* key, int32_t value);
    esp_err_t readInt(const char* key, int32_t& value, int32_t default_value = 0);
    esp_err_t writeBool(const char* key, bool value);
    esp_err_t readBool(const char* key, bool& value, bool default_value = false);
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

// C-compatible interface
typedef void* nvs_manager_handle_t;

nvs_manager_handle_t nvs_manager_create(void);
void nvs_manager_destroy(nvs_manager_handle_t handle);
esp_err_t nvs_manager_begin(nvs_manager_handle_t handle);
esp_err_t nvs_manager_load_all(nvs_manager_handle_t handle);
const char* nvs_manager_get_device_name(nvs_manager_handle_t handle);

#ifdef __cplusplus
}
#endif
    