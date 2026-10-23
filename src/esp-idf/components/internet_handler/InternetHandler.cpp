#include "InternetHandler.h"
#include "WiFiManager.h"
#include "NVSManager.h"
#include "TimeHandler.h"
#include "WeatherHandler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>

static const char* TAG = "InternetHandler";

InternetHandler::InternetHandler(WiFiManager* wifiManager, NVSManager* nvsManager)
    : wifiManager(wifiManager),
      timeHandler(nullptr),
      weatherHandler(nullptr),
      internetAvailable(false),
      wifiWasConnected(false),
      everChecked(false),
      checkSocket(-1),
      checkStartedMs(0),
      lastInternetCheck(0),
      lastWeatherRequest(0),
      weatherRequestedOnce(false) {

    // NET-07: the offset and the NTP servers come from Kconfig, not from literals.
    timeHandler = new TimeHandler(CONFIG_QNOB_UTC_OFFSET_MINUTES, CONFIG_QNOB_NTP_SERVER_1, CONFIG_QNOB_NTP_SERVER_2);

    // NET-28 with main's device settings: a weather key, city or country saved on the device (the
    // configureWeather command, the portal) wins; otherwise Kconfig, whose key is empty unless the
    // untracked bench overlay sets one. There is no literal fallback: the old key is burned
    // (dev_notes/credential-rotation.md).
    const std::string weatherToken = (nvsManager != nullptr && !nvsManager->weatherApiToken.empty())
        ? nvsManager->weatherApiToken
        : std::string(CONFIG_QNOB_WEATHER_API_KEY);
    const std::string weatherCity = (nvsManager != nullptr && !nvsManager->weatherCity.empty())
        ? nvsManager->weatherCity
        : std::string(CONFIG_QNOB_WEATHER_CITY);
    const std::string weatherCountry = (nvsManager != nullptr && !nvsManager->weatherCountryCode.empty())
        ? nvsManager->weatherCountryCode
        : std::string(CONFIG_QNOB_WEATHER_COUNTRY);
    weatherHandler = new WeatherHandler(weatherToken, weatherCity, weatherCountry);

    // Set up the internet check callback for WeatherHandler
    weatherHandler->setInternetCheckCallback([this]() {
        return this->isInternetAvailable();
    });

    ESP_LOGI(TAG, "InternetHandler initialized");
}

InternetHandler::~InternetHandler() {
    abortInternetCheck();

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

void InternetHandler::requestInternetCheck() {
    lastInternetCheck = 0;  // the next update() starts a probe
}

bool InternetHandler::checkInternetConnectivity() {
    requestInternetCheck();
    return isInternetAvailable();
}

void InternetHandler::startInternetCheck(int64_t now) {
    if (checkSocket >= 0) {
        return;  // a probe is already in flight
    }

    // A non-blocking TCP connect to a public DNS server on port 53; the answer is read by
    // serviceInternetCheck() on later iterations, so the network task never waits here.
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        setInternetAvailable(false, now);
        lastInternetCheck = now;
        return;
    }

    const int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(53);
    inet_aton("8.8.8.8", &dest_addr.sin_addr);

    const int result = connect(sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (result == 0) {
        close(sock);
        setInternetAvailable(true, now);
        lastInternetCheck = now;
        return;
    }
    if (errno != EINPROGRESS) {
        close(sock);
        setInternetAvailable(false, now);
        lastInternetCheck = now;
        return;
    }

    checkSocket = sock;
    checkStartedMs = now;
    everChecked = true;
}

void InternetHandler::serviceInternetCheck(int64_t now) {
    if (checkSocket < 0) {
        return;
    }

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(checkSocket, &writeSet);
    struct timeval zero = {0, 0};
    const int ready = select(checkSocket + 1, nullptr, &writeSet, nullptr, &zero);

    if (ready > 0 && FD_ISSET(checkSocket, &writeSet)) {
        int soError = 0;
        socklen_t len = sizeof(soError);
        getsockopt(checkSocket, SOL_SOCKET, SO_ERROR, &soError, &len);
        close(checkSocket);
        checkSocket = -1;
        lastInternetCheck = now;
        setInternetAvailable(soError == 0, now);
        return;
    }

    if (ready < 0 || now - checkStartedMs > INTERNET_CHECK_TIMEOUT) {
        close(checkSocket);
        checkSocket = -1;
        lastInternetCheck = now;
        setInternetAvailable(false, now);
    }
}

void InternetHandler::abortInternetCheck() {
    if (checkSocket >= 0) {
        close(checkSocket);
        checkSocket = -1;
    }
}

void InternetHandler::setInternetAvailable(bool available, int64_t now) {
    const bool previous = internetAvailable.exchange(available, std::memory_order_acq_rel);
    if (previous == available) {
        return;
    }
    if (available) {
        ESP_LOGI(TAG, "Internet connectivity established");
        // Trigger initial updates: a clock sync when the RTC is not set, and a weather fetch.
        if (timeHandler && !timeHandler->isTimeInitialized()) {
            updateDateTime();
        }
        updateWeather();
        lastWeatherRequest = now;
        weatherRequestedOnce = true;
    } else {
        ESP_LOGW(TAG, "Internet connectivity lost");
    }
}

bool InternetHandler::updateDateTime() {
    if (!isInternetAvailable()) {
        ESP_LOGD(TAG, "Cannot update time - No internet connection");
        return false;
    }

    ESP_LOGI(TAG, "Starting NTP sync");
    if (timeHandler) {
        timeHandler->startSync();
        return true;
    }
    return false;
}

bool InternetHandler::updateWeather() {
    if (!isInternetAvailable()) {
        ESP_LOGD(TAG, "Cannot update weather - No internet connection");
        return false;
    }

    ESP_LOGI(TAG, "Requesting weather data");
    return weatherHandler ? weatherHandler->requestUpdate() : false;
}

void InternetHandler::update() {
    const int64_t currentMillis = millis();
    const bool wifiConnected = wifiManager && wifiManager->isConnected();

    if (!wifiConnected) {
        abortInternetCheck();
        setInternetAvailable(false, currentMillis);
        wifiWasConnected = false;
        if (weatherHandler) {
            weatherHandler->service(currentMillis);  // lets an in-flight fetch fail cleanly
        }
        return;
    }

    // Probe immediately when the link comes up (MQTT waits on the answer), then every 30 s.
    if (!wifiWasConnected) {
        wifiWasConnected = true;
        startInternetCheck(currentMillis);
    } else if (checkSocket < 0 && currentMillis - lastInternetCheck >= INTERNET_CHECK_INTERVAL) {
        startInternetCheck(currentMillis);
    }
    serviceInternetCheck(currentMillis);

    if (timeHandler) {
        timeHandler->update();
    }

    if (isInternetAvailable() && weatherHandler) {
        if (!weatherRequestedOnce || currentMillis - lastWeatherRequest >= WEATHER_CHECK_INTERVAL) {
            weatherHandler->requestUpdate();
            lastWeatherRequest = currentMillis;
            weatherRequestedOnce = true;
        }
    }
    if (weatherHandler) {
        weatherHandler->service(currentMillis);
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
