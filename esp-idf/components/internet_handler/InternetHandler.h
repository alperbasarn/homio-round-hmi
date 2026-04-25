#pragma once

#include <string>

// Forward declarations
class WiFiManager;
class TimeHandler;
class WeatherHandler;
class NVSManager;

class InternetHandler {
private:
    WiFiManager* wifiManager;
    TimeHandler* timeHandler;
    WeatherHandler* weatherHandler;

    bool internetAvailable;
    int64_t lastInternetCheck;
    int64_t lastTimeCheck;
    int64_t lastWeatherCheck;

    static constexpr int64_t INTERNET_CHECK_INTERVAL = 30000;   // 30 seconds
    static constexpr int64_t WEATHER_CHECK_INTERVAL = 300000;   // 5 minutes
    static constexpr int64_t TIME_SYNC_INTERVAL = 3600000;      // 1 hour

    int64_t millis() const;

public:
    InternetHandler(WiFiManager* wifiManager, NVSManager* nvsManager = nullptr);
    ~InternetHandler();

    // Connectivity
    bool checkInternetConnectivity();
    bool isInternetAvailable() const { return internetAvailable; }

    // Getters for handlers
    TimeHandler* getTimeHandler() const { return timeHandler; }
    WeatherHandler* getWeatherHandler() const { return weatherHandler; }

    // Data update methods
    bool updateDateTime();
    bool updateWeather();

    // Convenience getters
    std::string getCurrentDate() const;
    std::string getCurrentTime() const;
    std::string getDayOfWeek() const;
    float getOutdoorTemperature() const;
    std::string getWeatherTemperature() const;

    // Main update loop
    void update();
};
