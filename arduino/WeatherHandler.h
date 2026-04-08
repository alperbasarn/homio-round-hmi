#ifndef WEATHER_HANDLER_H
#define WEATHER_HANDLER_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

// Forward declaration
class InternetHandler;

class WeatherHandler {
private:
    InternetHandler* internetHandler;  // Reference to parent InternetHandler
    
    // Weather data
    String weatherTemp = "--°C";     // Current temperature
    int humidity = 0;                // Humidity percentage
    float windSpeed = 0;             // Wind speed in m/s
    String condition = "Unknown";    // Weather condition (Clear, Clouds, etc.)
    
    // API configuration
    String apiKey;
    String city;
    String countryCode;
    bool configValid = false;

public:
    WeatherHandler(InternetHandler* internet, 
                  const String& apiKey = "b105aa78626cfb20db2d2669aaa09cd7",
                  const String& city = "Nagasaki", 
                  const String& countryCode = "jp");
    ~WeatherHandler() = default;

    // Core functionality
    bool updateWeather();
    
    // Getters
    String getWeatherTemperature() const;
    int getHumidity() const;
    float getWindSpeed() const;
    String getCondition() const;
    
    // Configuration
    void setLocation(const String& newCity, const String& newCountryCode);
    void setApiKey(const String& newApiKey);
    
    // Update method - checks and updates if needed
    void update();
};

#endif // WEATHER_HANDLER_H