#include "MQTTManager.h"
#include "WiFiManager.h"
#include "NVSManager.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include <cstring>

static const char *TAG = "MQTTManager";

// Static instance pointer for event handler
static MQTTManager* s_mqtt_manager = nullptr;

namespace {
std::string maskSecret(const std::string& value)
{
    if (value.empty()) {
        return "(empty)";
    }
    if (value.size() <= 2) {
        return std::string(value.size(), '*');
    }
    return value.substr(0, 1) + std::string(value.size() - 2, '*') + value.substr(value.size() - 1);
}
}  // namespace

MQTTManager::MQTTManager(WiFiManager* wifiManager, NVSManager* nvsManager)
    : wifi_manager(wifiManager),
      nvs_manager(nvsManager),
      mqtt_client(nullptr),
      initialized(false),
      mqtt_connected(false),
      use_light_config(false),
      has_sound_message(false),
      broker_port(8883),
      last_connect_attempt(0),
      on_message(nullptr)
{
    s_mqtt_manager = this;
}

MQTTManager::~MQTTManager()
{
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
    }
    s_mqtt_manager = nullptr;
}

void MQTTManager::mqttEventHandler(void* handler_args, esp_event_base_t base,
                                    int32_t event_id, void* event_data)
{
    if (s_mqtt_manager) {
        s_mqtt_manager->handleMQTTEvent((esp_mqtt_event_handle_t)event_data);
    }
}

void MQTTManager::handleMQTTEvent(esp_mqtt_event_handle_t event)
{
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker");
            mqtt_connected = true;

            // Subscribe to default topics
            subscribe(MQTT_TOPIC_SOUND_CONTROL);
            subscribe(MQTT_TOPIC_SOUND_SETPOINT);
            subscribe(MQTT_TOPIC_SOUND_RESPONSE);
            subscribe(MQTT_TOPIC_COMMAND);
            subscribe(MQTT_TOPIC_INDOOR_TEMP);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected from broker");
            mqtt_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGD(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT message published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA: {
            // Extract topic and data
            std::string topic(event->topic, event->topic_len);
            std::string payload(event->data, event->data_len);

            ESP_LOGI(TAG, "MQTT message received - Topic: %s", topic.c_str());
            ESP_LOGD(TAG, "Payload: %s", payload.c_str());

            processIncomingMessage(topic, payload);

            if (on_message) {
                on_message(topic, payload);
            }
            break;
        }

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP transport error: %s", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;

        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT connecting to broker...");
            break;

        default:
            ESP_LOGD(TAG, "MQTT event: %d", event->event_id);
            break;
    }
}

std::string MQTTManager::generateClientId()
{
    std::string device_name = "QNOB";
    if (nvs_manager && !nvs_manager->deviceName.empty()) {
        device_name = nvs_manager->deviceName.substr(0, 8);
    }

    char client_id[32];
    snprintf(client_id, sizeof(client_id), "ESP32-%s-%04X",
             device_name.c_str(), (uint16_t)(esp_random() & 0xFFFF));

    return std::string(client_id);
}

void MQTTManager::loadConfig()
{
    if (!nvs_manager) {
        ESP_LOGE(TAG, "NVS manager not set");
        return;
    }

    if (use_light_config) {
        broker_url = nvs_manager->lightMQTTServerURL;
        broker_port = nvs_manager->lightMQTTServerPort;
        username = nvs_manager->lightMQTTUsername;
        password = nvs_manager->lightMQTTPassword;
        ESP_LOGI(TAG, "Using Light MQTT configuration");
    } else {
        broker_url = nvs_manager->soundMQTTServerURL;
        broker_port = nvs_manager->soundMQTTServerPort;
        username = nvs_manager->soundMQTTUsername;
        password = nvs_manager->soundMQTTPassword;
        ESP_LOGI(TAG, "Using Sound MQTT configuration");
    }

    if (broker_url.empty() && strlen(CONFIG_QNOB_MQTT_BROKER_URL) > 0) {
        broker_url = CONFIG_QNOB_MQTT_BROKER_URL;
        broker_port = CONFIG_QNOB_MQTT_BROKER_PORT;
        username.clear();
        password.clear();
        ESP_LOGI(TAG, "Using default MQTT configuration from Kconfig");
    }

    // Validate port
    if (broker_port <= 0 || broker_port > 65535) {
        broker_port = 8883;
    }

    ESP_LOGI(TAG, "MQTT Config - Broker: %s, Port: %d, User: %s",
             broker_url.c_str(), broker_port,
             username.empty() ? "(none)" : username.c_str());
    ESP_LOGI(TAG, "MQTT Auth - User: %s, Pass: %s",
             username.empty() ? "(none)" : username.c_str(),
             maskSecret(password).c_str());
    ESP_LOGD(TAG, "MQTT Auth (debug) - User: %s, Pass: %s",
             username.c_str(), password.c_str());
}

esp_err_t MQTTManager::initialize(bool useLightConfig)
{
    use_light_config = useLightConfig;
    loadConfig();

    if (broker_url.empty()) {
        ESP_LOGW(TAG, "MQTT broker URL not configured");
        return ESP_ERR_INVALID_ARG;
    }

    client_name = generateClientId();

    // Build the full URI
    std::string uri;
    if (broker_url.find("://") == std::string::npos) {
        // Add protocol if not present
        if (broker_port == 8883 || broker_port == 443) {
            uri = "mqtts://" + broker_url;
        } else {
            uri = "mqtt://" + broker_url;
        }
    } else {
        uri = broker_url;
    }

    // Add port if not in URI
    if (uri.find(":", uri.find("://") + 3) == std::string::npos) {
        uri += ":" + std::to_string(broker_port);
    }

    ESP_LOGI(TAG, "MQTT URI: %s", uri.c_str());
    ESP_LOGI(TAG, "Client ID: %s", client_name.c_str());

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = uri.c_str();
    mqtt_cfg.credentials.client_id = client_name.c_str();

    if (!username.empty()) {
        mqtt_cfg.credentials.username = username.c_str();
    }
    if (!password.empty()) {
        mqtt_cfg.credentials.authentication.password = password.c_str();
    }

    // Buffer sizes
    mqtt_cfg.buffer.size = MQTT_MAX_PACKET_SIZE;
    mqtt_cfg.buffer.out_size = MQTT_MAX_PACKET_SIZE;

    // Network timeout
    mqtt_cfg.network.timeout_ms = MQTT_CONNECTION_TIMEOUT_MS;

    // TLS verification
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    // Skip hostname validation for development (insecure)
    mqtt_cfg.broker.verification.skip_cert_common_name_check = true;

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    // Register event handler
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                    mqttEventHandler, this);

    initialized = true;
    ESP_LOGI(TAG, "MQTT client initialized");

    return ESP_OK;
}

esp_err_t MQTTManager::connect()
{
    if (!initialized) {
        ESP_LOGE(TAG, "MQTT not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (mqtt_connected) {
        ESP_LOGD(TAG, "Already connected to MQTT broker");
        return ESP_OK;
    }

    // Check WiFi/Internet
    if (wifi_manager && !wifi_manager->isInternetAvailable()) {
        ESP_LOGW(TAG, "No internet connectivity, skipping MQTT connect");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    int64_t current_time = esp_timer_get_time() / 1000;
    if (current_time - last_connect_attempt < MQTT_RECONNECT_DELAY_MS) {
        return ESP_ERR_TIMEOUT;
    }
    last_connect_attempt = current_time;

    ESP_LOGI(TAG, "Starting MQTT client...");
    esp_err_t ret = esp_mqtt_client_start(mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
    }

    return ret;
}

void MQTTManager::disconnect()
{
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        mqtt_connected = false;
    }
}

esp_err_t MQTTManager::publish(const std::string& topic, const std::string& message, int qos, bool retain)
{
    if (!mqtt_connected) {
        ESP_LOGW(TAG, "Not connected, cannot publish to %s", topic.c_str());
        return ESP_ERR_INVALID_STATE;
    }

    // Add sender ID to message
    std::string device_name = nvs_manager ? nvs_manager->deviceName : "QNOB";
    std::string full_message = "sender=" + device_name + "," + message;

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic.c_str(),
                                          full_message.c_str(), full_message.length(),
                                          qos, retain ? 1 : 0);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish message");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published to %s: %s (msg_id=%d)", topic.c_str(), message.c_str(), msg_id);
    return ESP_OK;
}

esp_err_t MQTTManager::subscribe(const std::string& topic, int qos)
{
    if (!mqtt_connected) {
        ESP_LOGW(TAG, "Not connected, cannot subscribe to %s", topic.c_str());
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic.c_str(), qos);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe to %s", topic.c_str());
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Subscribed to %s (msg_id=%d)", topic.c_str(), msg_id);
    return ESP_OK;
}

esp_err_t MQTTManager::unsubscribe(const std::string& topic)
{
    if (!mqtt_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_unsubscribe(mqtt_client, topic.c_str());
    if (msg_id < 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Unsubscribed from %s (msg_id=%d)", topic.c_str(), msg_id);
    return ESP_OK;
}

void MQTTManager::processIncomingMessage(const std::string& topic, const std::string& payload)
{
    // Check if this message is from ourselves
    std::string device_name = nvs_manager ? nvs_manager->deviceName : "QNOB";
    std::string sender_prefix = "sender=" + device_name + ",";

    if (payload.find(sender_prefix) == 0) {
        ESP_LOGD(TAG, "Ignoring message from self");
        return;
    }

    // Extract actual message (remove sender prefix if present)
    std::string actual_message = payload;
    size_t comma_pos = payload.find(',');
    if (payload.find("sender=") == 0 && comma_pos != std::string::npos) {
        actual_message = payload.substr(comma_pos + 1);
    }

    // Check for sound-related topics
    if (topic == MQTT_TOPIC_SOUND_CONTROL ||
        topic.find("esp32/sound/") == 0) {
        sound_message = actual_message;
        has_sound_message = true;
        ESP_LOGI(TAG, "Sound message received: %s", sound_message.c_str());
    }
}

std::string MQTTManager::getSoundMessage()
{
    has_sound_message = false;
    return sound_message;
}

bool MQTTManager::hasSoundMQTTConfigured() const
{
    return nvs_manager && !nvs_manager->soundMQTTServerURL.empty();
}

bool MQTTManager::hasLightMQTTConfigured() const
{
    return nvs_manager && !nvs_manager->lightMQTTServerURL.empty();
}

void MQTTManager::update()
{
    if (!initialized) return;

    // Check WiFi/Internet connectivity
    bool internet_available = wifi_manager && wifi_manager->isInternetAvailable();

    if (!internet_available) {
        if (mqtt_connected) {
            ESP_LOGW(TAG, "Internet lost, MQTT will disconnect");
            disconnect();
        }
        return;
    }

    // Try to connect if not connected
    if (!mqtt_connected && !broker_url.empty()) {
        connect();
    }
}

// C-compatible interface
extern "C" {

mqtt_manager_handle_t mqtt_manager_create(void* wifi_manager, void* nvs_manager)
{
    return new MQTTManager(static_cast<WiFiManager*>(wifi_manager),
                           static_cast<NVSManager*>(nvs_manager));
}

void mqtt_manager_destroy(mqtt_manager_handle_t handle)
{
    delete static_cast<MQTTManager*>(handle);
}

esp_err_t mqtt_manager_init(mqtt_manager_handle_t handle, bool use_light_config)
{
    return static_cast<MQTTManager*>(handle)->initialize(use_light_config);
}

esp_err_t mqtt_manager_connect(mqtt_manager_handle_t handle)
{
    return static_cast<MQTTManager*>(handle)->connect();
}

bool mqtt_manager_is_connected(mqtt_manager_handle_t handle)
{
    return static_cast<MQTTManager*>(handle)->isConnected();
}

esp_err_t mqtt_manager_publish(mqtt_manager_handle_t handle, const char* topic, const char* message)
{
    return static_cast<MQTTManager*>(handle)->publish(topic, message);
}

}
