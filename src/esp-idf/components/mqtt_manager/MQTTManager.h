#pragma once

#include <string>
#include <functional>
#include <vector>
#include "esp_err.h"
#include "mqtt_client.h"

// Forward declarations
class WiFiManager;
class NVSManager;

// MQTT message callback
using MQTTMessageCallback = std::function<void(const std::string& topic, const std::string& payload)>;

// Configuration
#define MQTT_RECONNECT_DELAY_MS     5000
#define MQTT_CONNECTION_TIMEOUT_MS  10000
#define MQTT_MAX_PACKET_SIZE        1024

// MQTT Topics
#define MQTT_TOPIC_SOUND_CONTROL    "esp32/sound/control"
#define MQTT_TOPIC_SOUND_SETPOINT   "esp32/sound/setpoint"
#define MQTT_TOPIC_SOUND_RESPONSE   "esp32/sound/response"
#define MQTT_TOPIC_LIGHT_BRIGHTNESS "esp32/light/brightness"
#define MQTT_TOPIC_LIGHT_CONTROL    "esp32/light/control"
#define MQTT_TOPIC_COMMAND          "esp32/command"
#define MQTT_TOPIC_COMMAND_RESPONSE "esp32/command/response"
#define MQTT_TOPIC_INDOOR_TEMP      "qnob/indoor/temperature"

#ifdef __cplusplus

class MQTTManager {
public:
    MQTTManager(WiFiManager* wifiManager, NVSManager* nvsManager);
    ~MQTTManager();

    // Initialization
    esp_err_t initialize(bool useLightConfig = false);

    // Connection
    esp_err_t connect();
    void disconnect();
    bool isConnected() const { return mqtt_connected; }

    // Messaging
    esp_err_t publish(const std::string& topic, const std::string& message, int qos = 0, bool retain = false);
    esp_err_t subscribe(const std::string& topic, int qos = 0);
    esp_err_t unsubscribe(const std::string& topic);

    // Sound message handling (for backward compatibility with Arduino version)
    bool hasSoundMessage() const { return has_sound_message; }
    std::string getSoundMessage();

    // Callbacks
    void setMessageCallback(MQTTMessageCallback callback) { on_message = callback; }

    // Configuration check
    bool hasSoundMQTTConfigured() const;
    bool hasLightMQTTConfigured() const;

    // Update - call regularly
    void update();

    // Get client name
    std::string getClientName() const { return client_name; }

private:
    WiFiManager* wifi_manager;
    NVSManager* nvs_manager;
    esp_mqtt_client_handle_t mqtt_client;

    bool initialized;
    bool mqtt_connected;
    bool use_light_config;
    bool has_sound_message;

    std::string broker_url;
    int broker_port;
    std::string username;
    std::string password;
    std::string client_name;

    std::string sound_message;
    int64_t last_connect_attempt;

    MQTTMessageCallback on_message;

    // Event handler
    static void mqttEventHandler(void* handler_args, esp_event_base_t base,
                                  int32_t event_id, void* event_data);
    void handleMQTTEvent(esp_mqtt_event_handle_t event);

    // Helper methods
    std::string generateClientId();
    void loadConfig();
    void processIncomingMessage(const std::string& topic, const std::string& payload);
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

// C-compatible interface
typedef void* mqtt_manager_handle_t;

mqtt_manager_handle_t mqtt_manager_create(void* wifi_manager, void* nvs_manager);
void mqtt_manager_destroy(mqtt_manager_handle_t handle);
esp_err_t mqtt_manager_init(mqtt_manager_handle_t handle, bool use_light_config);
esp_err_t mqtt_manager_connect(mqtt_manager_handle_t handle);
bool mqtt_manager_is_connected(mqtt_manager_handle_t handle);
esp_err_t mqtt_manager_publish(mqtt_manager_handle_t handle, const char* topic, const char* message);

#ifdef __cplusplus
}
#endif
