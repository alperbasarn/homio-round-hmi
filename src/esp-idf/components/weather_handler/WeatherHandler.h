#pragma once

#include <string>
#include <functional>

// Callback to check internet availability
using InternetCheckCallback = std::function<bool()>;

class WeatherHandler {
private:
    InternetCheckCallback checkInternet;

    // Weather data
    float temperature;
    int humidity;
    float windSpeed;
    std::string condition;

    // API configuration
    std::string apiKey;
    std::string city;
    std::string countryCode;
    bool configValid;

    int64_t millis() const;

public:
    WeatherHandler(const std::string& apiKey = "b105aa78626cfb20db2d2669aaa09cd7",
                   const std::string& city = "Istanbul",
                   const std::string& countryCode = "tr");
    ~WeatherHandler() = default;

    // Set internet check callback
    void setInternetCheckCallback(InternetCheckCallback callback) { checkInternet = callback; }

    // Core functionality
    bool updateWeather();

    // Getters
    float getTemperature() const { return temperature; }
    std::string getTemperatureString() const;
    int getHumidity() const { return humidity; }
    float getWindSpeed() const { return windSpeed; }
    std::string getCondition() const { return condition; }

    // Configuration
    void setLocation(const std::string& newCity, const std::string& newCountryCode);
    void setApiKey(const std::string& newApiKey);

    // Update method
    void update();
};
