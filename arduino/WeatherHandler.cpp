#include "WeatherHandler.h"
#include "InternetHandler.h"
#include <WiFiClient.h>

WeatherHandler::WeatherHandler(InternetHandler* internet,
                               const String& apiKey,
                               const String& city,
                               const String& countryCode)
  : internetHandler(internet),
    apiKey(apiKey),
    city(city),
    countryCode(countryCode),
    humidity(0),
    windSpeed(0),
    condition("Unknown") {

  // Check if the API key and location are valid
  configValid = (apiKey.length() > 0 && city.length() > 0 && countryCode.length() > 0);

  Serial.println("WeatherHandler initialized");
  if (configValid) {
    Serial.println("Weather location: " + city + ", " + countryCode);
  } else {
    Serial.println("WARNING: Invalid weather API configuration");
  }
}

bool WeatherHandler::updateWeather() {
  if (!internetHandler || !internetHandler->isInternetAvailable()) {
    Serial.println("Cannot update weather: no internet connection");
    return false;
  }

  if (!configValid) {
    Serial.println("Cannot update weather: invalid configuration");
    return false;
  }

  // Build API request URL
  String serverPath = "http://api.openweathermap.org/data/2.5/weather?q="
                      + city + "," + countryCode
                      + "&units=metric&appid=" + apiKey;

  Serial.println("Fetching weather data...");

  // Make the HTTP request
  HTTPClient http;
  http.begin(serverPath);

  // Add timeout to avoid hanging
  http.setTimeout(5000);

  // Send GET request
  int httpCode = http.GET();
  String payload = "{}";

  // Check for successful response
  if (httpCode > 0) {
    payload = http.getString();
    Serial.printf("HTTP Response code: %d\n", httpCode);
  } else {
    Serial.printf("HTTP Error code: %d\n", httpCode);
    http.end();
    return false;
  }

  // Parse JSON response
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    http.end();
    return false;
  }

  // Extract weather data
  if (doc.containsKey("main") && doc["main"].containsKey("temp")) {
    float temperature = doc["main"]["temp"];
    weatherTemp = String(int(round(temperature))) + "°C";

    // Extract additional data if available
    if (doc["main"].containsKey("humidity")) {
      humidity = doc["main"]["humidity"];
    }

    if (doc.containsKey("wind") && doc["wind"].containsKey("speed")) {
      windSpeed = doc["wind"]["speed"];
    }

    if (doc.containsKey("weather") && doc["weather"].size() > 0 && doc["weather"][0].containsKey("main")) {
      condition = doc["weather"][0]["main"].as<String>();
    }

    // Log the updated data
    Serial.println("----- Weather Updated -----");
    Serial.println("Temperature: " + weatherTemp);
    Serial.println("Humidity: " + String(humidity) + "%");
    Serial.println("Wind Speed: " + String(windSpeed) + " m/s");
    Serial.println("Conditions: " + condition);
    Serial.println("--------------------------");

    http.end();
    return true;
  } else {
    Serial.println("Error: Could not find temperature data in the response");
    http.end();
    return false;
  }
}

String WeatherHandler::getWeatherTemperature() const {
  return weatherTemp;
}

int WeatherHandler::getHumidity() const {
  return humidity;
}

float WeatherHandler::getWindSpeed() const {
  return windSpeed;
}

String WeatherHandler::getCondition() const {
  return condition;
}

void WeatherHandler::setLocation(const String& newCity, const String& newCountryCode) {
  city = newCity;
  countryCode = newCountryCode;
  configValid = (apiKey.length() > 0 && city.length() > 0 && countryCode.length() > 0);

  // Force update on next check
}

void WeatherHandler::setApiKey(const String& newApiKey) {
  apiKey = newApiKey;
  configValid = (apiKey.length() > 0 && city.length() > 0 && countryCode.length() > 0);

  // Force update on next check
}

void WeatherHandler::update() {
  updateWeather();
}