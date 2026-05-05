#include "WeatherHandler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <cstring>
#include <cmath>

static const char* TAG = "WeatherHandler";

// Buffer for HTTP response
static char http_response_buffer[2048];
static int http_response_len = 0;

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (http_response_len + evt->data_len < sizeof(http_response_buffer) - 1) {
                    memcpy(http_response_buffer + http_response_len, evt->data, evt->data_len);
                    http_response_len += evt->data_len;
                    http_response_buffer[http_response_len] = '\0';
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

WeatherHandler::WeatherHandler(const std::string& apiKey,
                               const std::string& city,
                               const std::string& countryCode)
    : checkInternet(nullptr),
      temperature(0),
      humidity(0),
      windSpeed(0),
      condition("Unknown"),
      apiKey(apiKey),
      city(city),
      countryCode(countryCode) {

    configValid = (!apiKey.empty() && !city.empty() && !countryCode.empty());

    ESP_LOGI(TAG, "WeatherHandler initialized");
    if (configValid) {
        ESP_LOGI(TAG, "Weather location: %s, %s", city.c_str(), countryCode.c_str());
    } else {
        ESP_LOGW(TAG, "Invalid weather API configuration");
    }
}

int64_t WeatherHandler::millis() const {
    return esp_timer_get_time() / 1000;
}

bool WeatherHandler::updateWeather() {
    // Check internet availability via callback
    if (checkInternet && !checkInternet()) {
        ESP_LOGW(TAG, "Cannot update weather: no internet connection");
        return false;
    }

    if (!configValid) {
        ESP_LOGW(TAG, "Cannot update weather: invalid configuration");
        return false;
    }

    // Build API request URL
    char url[256];
    snprintf(url, sizeof(url),
             "http://api.openweathermap.org/data/2.5/weather?q=%s,%s&units=metric&appid=%s",
             city.c_str(), countryCode.c_str(), apiKey.c_str());

    ESP_LOGI(TAG, "Fetching weather data...");

    // Reset response buffer
    http_response_len = 0;
    memset(http_response_buffer, 0, sizeof(http_response_buffer));

    // Configure HTTP client
    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    // Perform request
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP Status: %d, Response length: %d", status_code, http_response_len);

    esp_http_client_cleanup(client);

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP request failed with status %d", status_code);
        return false;
    }

    // Parse JSON response
    cJSON *root = cJSON_Parse(http_response_buffer);
    if (root == nullptr) {
        ESP_LOGE(TAG, "JSON parsing failed");
        return false;
    }

    bool success = false;

    // Extract temperature from main.temp
    cJSON *main = cJSON_GetObjectItem(root, "main");
    if (main != nullptr) {
        cJSON *temp = cJSON_GetObjectItem(main, "temp");
        if (temp != nullptr && cJSON_IsNumber(temp)) {
            temperature = (float)temp->valuedouble;
            success = true;
        }

        cJSON *hum = cJSON_GetObjectItem(main, "humidity");
        if (hum != nullptr && cJSON_IsNumber(hum)) {
            humidity = hum->valueint;
        }
    }

    // Extract wind speed
    cJSON *wind = cJSON_GetObjectItem(root, "wind");
    if (wind != nullptr) {
        cJSON *speed = cJSON_GetObjectItem(wind, "speed");
        if (speed != nullptr && cJSON_IsNumber(speed)) {
            windSpeed = (float)speed->valuedouble;
        }
    }

    // Extract weather condition
    cJSON *weather = cJSON_GetObjectItem(root, "weather");
    if (weather != nullptr && cJSON_IsArray(weather) && cJSON_GetArraySize(weather) > 0) {
        cJSON *first = cJSON_GetArrayItem(weather, 0);
        if (first != nullptr) {
            cJSON *main_cond = cJSON_GetObjectItem(first, "main");
            if (main_cond != nullptr && cJSON_IsString(main_cond)) {
                condition = std::string(main_cond->valuestring);
            }
        }
    }

    cJSON_Delete(root);

    if (success) {
        ESP_LOGI(TAG, "----- Weather Updated -----");
        ESP_LOGI(TAG, "Temperature: %.1f C", temperature);
        ESP_LOGI(TAG, "Humidity: %d%%", humidity);
        ESP_LOGI(TAG, "Wind Speed: %.1f m/s", windSpeed);
        ESP_LOGI(TAG, "Conditions: %s", condition.c_str());
        ESP_LOGI(TAG, "--------------------------");
    }

    return success;
}

std::string WeatherHandler::getTemperatureString() const {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)std::round(temperature));
    return std::string(buf);
}

void WeatherHandler::setLocation(const std::string& newCity, const std::string& newCountryCode) {
    city = newCity;
    countryCode = newCountryCode;
    configValid = (!apiKey.empty() && !city.empty() && !countryCode.empty());
}

void WeatherHandler::setApiKey(const std::string& newApiKey) {
    apiKey = newApiKey;
    configValid = (!apiKey.empty() && !city.empty() && !countryCode.empty());
}

void WeatherHandler::update() {
    updateWeather();
}
