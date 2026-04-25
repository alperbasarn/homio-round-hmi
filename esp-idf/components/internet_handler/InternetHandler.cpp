#include "InternetHandler.h"
#include "WiFiManager.h"
#include "NVSManager.h"
#include "TimeHandler.h"
#include "WeatherHandler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char* TAG = "InternetHandler";

InternetHandler::InternetHandler(WiFiManager* wifiManager, NVSManager* nvsManager)
    : wifiManager(wifiManager),
      timeHandler(nullptr),
      weatherHandler(nullptr),
      internetAvailable(false),
      lastInternetCheck(0),
      lastTimeCheck(0),
      lastWeatherCheck(0) {

    // Create TimeHandler (default timezone GMT+3 for Turkey)
    timeHandler = new TimeHandler(3);

    const std::string weatherToken = (nvsManager != nullptr && !nvsManager->weatherApiToken.empty())
        ? nvsManager->weatherApiToken
        : "b105aa78626cfb20db2d2669aaa09cd7";
    const std::string weatherCity = (nvsManager != nullptr && !nvsManager->weatherCity.empty())
        ? nvsManager->weatherCity
        : "Istanbul";
    const std::string weatherCountry = (nvsManager != nullptr && !nvsManager->weatherCountryCode.empty())
        ? nvsManager->weatherCountryCode
        : "tr";

    // Create WeatherHandler from persisted settings
    weatherHandler = new WeatherHandler(weatherToken, weatherCity, weatherCountry);

    // Set up the internet check callback for WeatherHandler
    weatherHandler->setInternetCheckCallback([this]() {
        return this->isInternetAvailable();
    });

    ESP_LOGI(TAG, "InternetHandler initialized");
}

InternetHandler::~InternetHandler() {
    if (timeHandler) {
        delete timeHandler;
        timeHandler = nullptr;
    }

    if (weatherHandler) {
        delete weatherHandler;
        weatherHandler = nullptr;
    }

    ESP_LOGI(TAG, "InternetHandler destroyed");
}

int64_t InternetHandler::millis() const {
    return esp_timer_get_time() / 1000;
}

bool InternetHandler::checkInternetConnectivity() {
    // Don't check if WiFi is not connected
    if (!wifiManager || !wifiManager->isConnected()) {
        ESP_LOGD(TAG, "Cannot check internet - WiFi not connected");
        internetAvailable = false;
        return false;
    }

    ESP_LOGD(TAG, "Checking internet connectivity...");

    // Try to connect to Google DNS (8.8.8.8) on port 53
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        internetAvailable = false;
        return false;
    }

    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(53);
    inet_aton("8.8.8.8", &dest_addr.sin_addr);

    bool connected = (connect(sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) == 0);
    close(sock);

    bool previousState = internetAvailable;
    internetAvailable = connected;

    if (previousState != internetAvailable) {
        if (internetAvailable) {
            ESP_LOGI(TAG, "Internet connectivity established");
            // Trigger initial updates
            if (timeHandler && !timeHandler->isTimeInitialized()) {
                updateDateTime();
            }
            updateWeather();
        } else {
            ESP_LOGW(TAG, "Internet connectivity lost");
        }
    }

    lastInternetCheck = millis();
    return internetAvailable;
}

bool InternetHandler::updateDateTime() {
    if (!internetAvailable) {
        ESP_LOGD(TAG, "Cannot update time - No internet connection");
        return false;
    }

    ESP_LOGI(TAG, "Syncing time with NTP");
    return timeHandler ? timeHandler->syncWithNTP() : false;
}

bool InternetHandler::updateWeather() {
    if (!internetAvailable) {
        ESP_LOGD(TAG, "Cannot update weather - No internet connection");
        return false;
    }

    ESP_LOGI(TAG, "Updating weather data");
    return weatherHandler ? weatherHandler->updateWeather() : false;
}

void InternetHandler::update() {
    int64_t currentMillis = millis();

    // Check internet connectivity periodically
    if (currentMillis - lastInternetCheck >= INTERNET_CHECK_INTERVAL) {
        checkInternetConnectivity();
        lastInternetCheck = currentMillis;
    }

    // Only perform updates if internet is available
    if (internetAvailable) {
        // Update time handler
        if (timeHandler) {
            timeHandler->update();
        }

        // Update weather periodically
        if (weatherHandler) {
            if (currentMillis - lastWeatherCheck >= WEATHER_CHECK_INTERVAL) {
                weatherHandler->update();
                lastWeatherCheck = currentMillis;
            }
        }
    }
}

std::string InternetHandler::getCurrentDate() const {
    return timeHandler ? timeHandler->getCurrentDate() : "----/--/--";
}

std::string InternetHandler::getCurrentTime() const {
    return timeHandler ? timeHandler->getCurrentTime() : "--:--";
}

std::string InternetHandler::getDayOfWeek() const {
    return timeHandler ? timeHandler->getDayOfWeek() : "---";
}

float InternetHandler::getOutdoorTemperature() const {
    return weatherHandler ? weatherHandler->getTemperature() : 0.0f;
}

std::string InternetHandler::getWeatherTemperature() const {
    return weatherHandler ? weatherHandler->getTemperatureString() : "--";
}
