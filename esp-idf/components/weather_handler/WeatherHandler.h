#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "esp_http_client.h"
#include "sdkconfig.h"

// Callback to check internet availability
using InternetCheckCallback = std::function<bool()>;

// OpenWeatherMap current conditions.
//
// NET-28: the key, city and country come from Kconfig (no literal key in the tree) and the
// request goes over HTTPS with the certificate bundle. NET-29: the response buffer belongs to the
// instance, chunked bodies are collected like any other, and an overflow is a logged failure
// rather than a parse of garbage. NET-30: the fetch is asynchronous; service() advances it from
// the network task without blocking.
class WeatherHandler {
private:
    InternetCheckCallback checkInternet;

    // Weather data
    float temperature;
    int humidity;
    float windSpeed;
    std::string condition;
    bool hasReading_;

    // API configuration
    std::string apiKey;
    std::string city;
    std::string countryCode;
    bool configValid;

    // In-flight request
    static constexpr size_t kResponseCapacity = 2048;
    static constexpr int64_t kRequestDeadlineMs = 10000;
    esp_http_client_handle_t client_;
    char responseBuffer_[kResponseCapacity];
    size_t responseLen_;
    bool truncated_;
    int64_t requestStartedMs_;
    int64_t lastSuccessMs_;

    int64_t millis() const;
    static esp_err_t httpEvent(esp_http_client_event_t* evt);
    void appendBody(const char* data, size_t len);
    void finishRequest(esp_err_t performResult);
    void abortRequest(const char* why);
    bool parseResponse();

public:
    WeatherHandler(const std::string& apiKey = CONFIG_QNOB_WEATHER_API_KEY,
                   const std::string& city = CONFIG_QNOB_WEATHER_CITY,
                   const std::string& countryCode = CONFIG_QNOB_WEATHER_COUNTRY);
    ~WeatherHandler();

    // Set internet check callback
    void setInternetCheckCallback(InternetCheckCallback callback) { checkInternet = callback; }

    // Core functionality
    bool requestUpdate();          // starts an async fetch; false when busy, unconfigured or offline
    void service(int64_t now);     // advances the fetch; call every network-task iteration
    bool isFetching() const { return client_ != nullptr; }
    bool updateWeather();          // compatibility: requestUpdate()

    // Getters
    bool hasReading() const { return hasReading_; }
    float getTemperature() const { return temperature; }
    std::string getTemperatureString() const;
    int getHumidity() const { return humidity; }
    float getWindSpeed() const { return windSpeed; }
    std::string getCondition() const { return condition; }
    int64_t lastSuccessMs() const { return lastSuccessMs_; }

    // Configuration
    void setLocation(const std::string& newCity, const std::string& newCountryCode);
    void setApiKey(const std::string& newApiKey);

    // Update method (compatibility: requestUpdate())
    void update();
};
