#include "WeatherHandler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <cstring>
#include <cmath>

static const char* TAG = "WeatherHandler";

WeatherHandler::WeatherHandler(const std::string& apiKey,
                               const std::string& city,
                               const std::string& countryCode)
    : checkInternet(nullptr),
      temperature(0),
      humidity(0),
      windSpeed(0),
      condition("Unknown"),
      hasReading_(false),
      apiKey(apiKey),
      city(city),
      countryCode(countryCode),
      client_(nullptr),
      responseLen_(0),
      truncated_(false),
      requestStartedMs_(0),
      lastSuccessMs_(0) {

    responseBuffer_[0] = '\0';
    configValid = (!apiKey.empty() && !city.empty() && !countryCode.empty());

    ESP_LOGI(TAG, "WeatherHandler initialized");
    if (configValid) {
        ESP_LOGI(TAG, "Weather location: %s, %s", city.c_str(), countryCode.c_str());
    } else {
        ESP_LOGW(TAG, "Weather not configured (CONFIG_QNOB_WEATHER_API_KEY empty): no fetch will run");
    }
}

WeatherHandler::~WeatherHandler() {
    abortRequest("destroyed");
}

int64_t WeatherHandler::millis() const {
    return esp_timer_get_time() / 1000;
}

esp_err_t WeatherHandler::httpEvent(esp_http_client_event_t* evt) {
    WeatherHandler* self = static_cast<WeatherHandler*>(evt->user_data);
    if (self == nullptr) {
        return ESP_OK;
    }
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != nullptr && evt->data_len > 0) {
        // Chunked or not, the body is appended the same way (NET-29).
        self->appendBody(static_cast<const char*>(evt->data), static_cast<size_t>(evt->data_len));
    }
    return ESP_OK;
}

void WeatherHandler::appendBody(const char* data, size_t len) {
    if (responseLen_ + len >= kResponseCapacity) {
        truncated_ = true;
        const size_t room = kResponseCapacity - 1 - responseLen_;
        memcpy(responseBuffer_ + responseLen_, data, room);
        responseLen_ += room;
    } else {
        memcpy(responseBuffer_ + responseLen_, data, len);
        responseLen_ += len;
    }
    responseBuffer_[responseLen_] = '\0';
}

bool WeatherHandler::requestUpdate() {
    if (client_ != nullptr) {
        ESP_LOGD(TAG, "Weather fetch already in flight");
        return false;
    }
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
             "https://api.openweathermap.org/data/2.5/weather?q=%s,%s&units=metric&appid=%s",
             city.c_str(), countryCode.c_str(), apiKey.c_str());

    ESP_LOGI(TAG, "Fetching weather data...");

    responseLen_ = 0;
    truncated_ = false;
    responseBuffer_[0] = '\0';

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = &WeatherHandler::httpEvent;
    config.user_data = this;
    config.timeout_ms = 5000;
    config.is_async = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    client_ = esp_http_client_init(&config);
    if (client_ == nullptr) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }
    requestStartedMs_ = millis();

    // The first step of an async request; later steps run from service().
    const esp_err_t err = esp_http_client_perform(client_);
    if (err != ESP_ERR_HTTP_EAGAIN) {
        finishRequest(err);
    }
    return true;
}

void WeatherHandler::service(int64_t now) {
    if (client_ == nullptr) {
        return;
    }
    if (now - requestStartedMs_ > kRequestDeadlineMs) {
        abortRequest("deadline");
        return;
    }
    const esp_err_t err = esp_http_client_perform(client_);
    if (err == ESP_ERR_HTTP_EAGAIN) {
        return;  // still in progress; try again on the next iteration
    }
    finishRequest(err);
}

void WeatherHandler::abortRequest(const char* why) {
    if (client_ == nullptr) {
        return;
    }
    ESP_LOGW(TAG, "Weather fetch aborted (%s)", why);
    esp_http_client_cleanup(client_);
    client_ = nullptr;
}

void WeatherHandler::finishRequest(esp_err_t performResult) {
    if (client_ == nullptr) {
        return;
    }
    const int status_code = esp_http_client_get_status_code(client_);
    esp_http_client_cleanup(client_);
    client_ = nullptr;

    if (performResult != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(performResult));
        return;
    }
    ESP_LOGI(TAG, "HTTP Status: %d, Response length: %u", status_code, static_cast<unsigned>(responseLen_));
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP request failed with status %d", status_code);
        return;
    }
    if (truncated_) {
        ESP_LOGE(TAG, "Weather response exceeded %u bytes; discarded", static_cast<unsigned>(kResponseCapacity));
        return;
    }
    if (parseResponse()) {
        hasReading_ = true;
        lastSuccessMs_ = millis();
    }
}

bool WeatherHandler::parseResponse() {
    cJSON *root = cJSON_Parse(responseBuffer_);
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

bool WeatherHandler::updateWeather() {
    return requestUpdate();
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
    requestUpdate();
}
