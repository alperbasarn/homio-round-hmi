#ifndef INTERNET_HANDLER_H
#define INTERNET_HANDLER_H

#include <Arduino.h>

// Forward declarations to resolve circular dependencies
class WiFiHandler;
class TimeHandler;
class WeatherHandler;

class InternetHandler {
private:
  WiFiHandler* wifiHandler;             // Pointer to parent WiFiHandler
  TimeHandler* timeHandler;             // Time functionality handler
  WeatherHandler* weatherHandler;       // Weather functionality handler
  
  bool internetAvailable = false;       // Internet connectivity status
  unsigned long lastInternetCheck = 0;  // Time of last internet connectivity check
  unsigned long lastTimeCheck = 0;      // Time of last time update check
  unsigned long lastWeatherCheck = 0;   // Time of last weather update check
  
  // Configuration values
  static const unsigned long INTERNET_CHECK_INTERVAL = 30000;   // 30 seconds
  static const unsigned long WEATHER_CHECK_INTERVAL = 300000;   // 5 minutes

public:
  /**
   * Constructor
   * 
   * @param wifiHandler Pointer to the WiFiHandler that manages WiFi connection
   */
  InternetHandler(WiFiHandler* wifiHandler);
  
  /**
   * Destructor - cleans up dynamic resources
   */
  ~InternetHandler();

  /**
   * Checks internet connectivity by attempting to connect to a reliable server
   * 
   * @return True if internet is available, false otherwise
   */
  bool checkInternetConnectivity();
  
  /**
   * Gets the current internet connectivity status
   * 
   * @return True if internet is available, false otherwise
   */
  bool isInternetAvailable() const {
    return internetAvailable;
  }

  /**
   * Gets the TimeHandler instance
   * 
   * @return Pointer to the TimeHandler instance
   */
  TimeHandler* getTimeHandler() const {
    return timeHandler;
  }
  
  /**
   * Gets the WeatherHandler instance
   * 
   * @return Pointer to the WeatherHandler instance
   */
  WeatherHandler* getWeatherHandler() const {
    return weatherHandler;
  }

  /**
   * Triggers a time update using NTP
   * Only works if internet is available
   * 
   * @return True if time was updated successfully, false otherwise
   */
  bool updateDateTime();
  
  /**
   * Triggers a weather update from weather service
   * Only works if internet is available
   * 
   * @return True if weather was updated successfully, false otherwise
   */
  bool updateWeather();

  /**
   * Gets the current date string (YYYY/MM/DD)
   * 
   * @return Current date string or placeholder if not available
   */
  String getCurrentDate() const;
  
  /**
   * Gets the current time string (HH:MM)
   * 
   * @return Current time string or placeholder if not available
   */
  String getCurrentTime() const;
  
  /**
   * Gets the current day of week (MON, TUE, etc.)
   * 
   * @return Current day of week or placeholder if not available
   */
  String getDayOfWeek() const;
  
  /**
   * Gets the current temperature from weather service
   * 
   * @return Current temperature string or placeholder if not available
   */
  String getWeatherTemperature() const;

  /**
   * Main update method - call this regularly from loop()
   * Manages periodic checks and updates for internet, time, and weather
   */
  void update();
};

#endif  // INTERNET_HANDLER_H