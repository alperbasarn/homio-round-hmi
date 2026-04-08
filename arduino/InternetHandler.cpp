#include "InternetHandler.h"
#include "WiFiHandler.h"
#include "TimeHandler.h"
#include "WeatherHandler.h"

InternetHandler::InternetHandler(WiFiHandler* wifiHandler)
  : wifiHandler(wifiHandler), internetAvailable(false), 
    lastInternetCheck(0), lastTimeCheck(0), lastWeatherCheck(0) {

  // Create TimeHandler (no dependency on InternetHandler)
  timeHandler = new TimeHandler();
  
  // Create WeatherHandler (needs reference to InternetHandler for internet status)
  weatherHandler = new WeatherHandler(this);

  Serial.println("InternetHandler initialized");
}

InternetHandler::~InternetHandler() {
  // Clean up dynamically allocated resources
  if (timeHandler) {
    delete timeHandler;
    timeHandler = nullptr;
  }
  
  if (weatherHandler) {
    delete weatherHandler;
    weatherHandler = nullptr;
  }
  
  Serial.println("InternetHandler destroyed");
}

bool InternetHandler::checkInternetConnectivity() {
  // Don't check internet connectivity if we're not connected to WiFi
  if (!wifiHandler || !wifiHandler->isWiFiConnected()) {
    Serial.println("DEBUG: Cannot check internet - WiFi not connected");
    internetAvailable = false;
    return false;
  }

  Serial.println("DEBUG: Checking internet connectivity...");

  // Use TCP connection to a reliable server as a better check
  WiFiClient testClient;
  testClient.setTimeout(3000);  // 3 second timeout - don't block too long

  // Try to connect to Google DNS server on port 53 (DNS)
  bool connected = testClient.connect("8.8.8.8", 53);

  // Close the connection regardless of result
  testClient.stop();

  // Update internal state
  bool previousState = internetAvailable;
  internetAvailable = connected;

  // Log change of state
  if (previousState != internetAvailable) {
    if (internetAvailable) {
      Serial.println("✅ Internet connectivity established. Getting weather data.");
      weatherHandler->update();
    } else {
      Serial.println("❌ Internet connectivity lost");
    }
  }

  lastInternetCheck = millis();
  return internetAvailable;
}

bool InternetHandler::updateDateTime() {
  if (!internetAvailable) {
    Serial.println("DEBUG: Cannot update time - No internet connection");
    return false;
  }

  Serial.println("DEBUG: Calling TimeHandler syncWithNTP");
  return timeHandler ? timeHandler->syncWithNTP() : false;
}

bool InternetHandler::updateWeather() {
  if (!internetAvailable) {
    Serial.println("DEBUG: Cannot update weather - No internet connection");
    return false;
  }

  Serial.println("DEBUG: Calling WeatherHandler updateWeather");
  return weatherHandler ? weatherHandler->updateWeather() : false;
}

void InternetHandler::update() {
  unsigned long currentMillis = millis();

  // Check internet connectivity periodically
  if (currentMillis - lastInternetCheck >= INTERNET_CHECK_INTERVAL) {
    checkInternetConnectivity();
    lastInternetCheck = currentMillis;
  }
  
  // Only perform updates if internet is available
  if (internetAvailable) {
    // Update time if we have a valid TimeHandler
    if (timeHandler) {
      timeHandler->update();
    } else {
      Serial.println("ERROR: timeHandler is NULL");
    }
    
    // Update weather periodically if we have a valid WeatherHandler
    if (weatherHandler) {
      if (currentMillis - lastWeatherCheck >= WEATHER_CHECK_INTERVAL) {
        weatherHandler->update();
        lastWeatherCheck = currentMillis;
      }
    } else {
      Serial.println("ERROR: weatherHandler is NULL");
    }
  } else {
    // Log this periodically but not on every update
    static unsigned long lastNoInternetLog = 0;
    if (currentMillis - lastNoInternetLog >= 10000) {  // Log every 10 seconds
      Serial.println("DEBUG: InternetHandler update - No internet, skipping updates");
      lastNoInternetLog = currentMillis;
    }
  }
}

String InternetHandler::getCurrentDate() const {
  return timeHandler ? timeHandler->getCurrentDate() : "----/--/--";
}

String InternetHandler::getCurrentTime() const {
  return timeHandler ? timeHandler->getCurrentTime() : "--:--";
}

String InternetHandler::getDayOfWeek() const {
  return timeHandler ? timeHandler->getDayOfWeek() : "---";
}

String InternetHandler::getWeatherTemperature() const {
  return weatherHandler ? weatherHandler->getWeatherTemperature() : "--°C";
}