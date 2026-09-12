#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// Forward declarations
class WiFiManager;
class TimeHandler;
class WeatherHandler;

// The single owner of "internet available" (NET-09). WiFiManager knows the link, this class
// probes the WAN with a non-blocking TCP connect (NET-30) on the network task, and MQTT reads
// the answer through an injected predicate. Time and weather requests are started here and
// serviced without blocking.
class InternetHandler {
private:
    WiFiManager* wifiManager;
    TimeHandler* timeHandler;
    WeatherHandler* weatherHandler;

    std::atomic<bool> internetAvailable;
    bool wifiWasConnected;
    bool everChecked;
    int checkSocket;
    int64_t checkStartedMs;
    int64_t lastInternetCheck;
    int64_t lastWeatherRequest;
    bool weatherRequestedOnce;

    static constexpr int64_t INTERNET_CHECK_INTERVAL = 30000;   // 30 seconds
    static constexpr int64_t INTERNET_CHECK_TIMEOUT = 3000;     // per probe
    static constexpr int64_t WEATHER_CHECK_INTERVAL = 300000;   // 5 minutes

    int64_t millis() const;
    void startInternetCheck(int64_t now);
    void serviceInternetCheck(int64_t now);
    void abortInternetCheck();
    void setInternetAvailable(bool available, int64_t now);

public:
    InternetHandler(WiFiManager* wifiManager);
    ~InternetHandler();

    // Connectivity
    bool isInternetAvailable() const { return internetAvailable.load(std::memory_order_acquire); }
    void requestInternetCheck();          // starts a probe on the next update()
    bool checkInternetConnectivity();     // compatibility: requests a probe, returns the last answer

    // Getters for handlers
    TimeHandler* getTimeHandler() const { return timeHandler; }
    WeatherHandler* getWeatherHandler() const { return weatherHandler; }

    // Data update requests (non-blocking)
    bool updateDateTime();
    bool updateWeather();

    // Convenience getters (copies; nothing is mutated)
    std::string getCurrentDate() const;
    std::string getCurrentTime() const;
    std::string getDayOfWeek() const;
    float getOutdoorTemperature() const;
    std::string getWeatherTemperature() const;

    // Main update loop (network task); never blocks
    void update();
};
