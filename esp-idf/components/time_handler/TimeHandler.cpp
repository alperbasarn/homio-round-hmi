#include "TimeHandler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* TAG = "TimeHandler";

TimeHandler::TimeHandler(int utcOffsetMinutes, const char* server1, const char* server2)
    : utcOffsetMinutes_(utcOffsetMinutes),
      lastNtpSyncTime(0),
      lastTimeCheckTime(0),
      syncStartedMs(0),
      timeInitialized(false),
      syncInFlight(false) {

    strcpy(currentDate, "----/--/--");
    strcpy(currentTime, "--:--");
    strcpy(dayOfWeek, "---");

    // POSIX TZ strings carry the sign inverted: UTC+3 is "UTC-3".
    char tzStr[32];
    const int absMinutes = utcOffsetMinutes < 0 ? -utcOffsetMinutes : utcOffsetMinutes;
    const char sign = utcOffsetMinutes >= 0 ? '-' : '+';
    if (absMinutes % 60 == 0) {
        snprintf(tzStr, sizeof(tzStr), "UTC%c%d", sign, absMinutes / 60);
    } else {
        snprintf(tzStr, sizeof(tzStr), "UTC%c%d:%02d", sign, absMinutes / 60, absMinutes % 60);
    }
    setenv("TZ", tzStr, 1);
    tzset();
    ESP_LOGI(TAG, "Clock offset %+d min (TZ=%s), NTP %s / %s", utcOffsetMinutes, tzStr, server1, server2);

    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server1);
    if (server2 != nullptr && server2[0] != '\0') {
        esp_sntp_setservername(1, server2);
    }

    // Check if RTC already has valid time (from previous boot)
    time_t now = time(nullptr);
    if (isTimeValid(now)) {
        ESP_LOGI(TAG, "Valid time detected in RTC memory");
        timeInitialized = true;
        updateTimeStrings();
    } else {
        ESP_LOGI(TAG, "RTC time not initialized yet");
        timeInitialized = false;
    }
}

TimeHandler::~TimeHandler() {
    esp_sntp_stop();
}

int64_t TimeHandler::millis() const {
    return esp_timer_get_time() / 1000;
}

bool TimeHandler::isTimeValid(time_t timeValue) const {
    return timeValue > VALID_TIME_THRESHOLD;
}

const char* TimeHandler::getDayOfWeekString(int wday) {
    static const char* days[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    if (wday >= 0 && wday <= 6) {
        return days[wday];
    }
    return "---";
}

void TimeHandler::startSync() {
    if (esp_sntp_enabled() == false) {
        esp_sntp_init();
    } else {
        esp_sntp_restart();
    }
    syncInFlight = true;
    syncStartedMs = millis();
    ESP_LOGI(TAG, "NTP sync started");
}

bool TimeHandler::syncWithNTP() {
    startSync();
    return timeInitialized;
}

time_t TimeHandler::epochNow() const {
    return time(nullptr);
}

time_t TimeHandler::getCurrentEpochTime() {
    time_t now = time(nullptr);

    if (!isTimeValid(now) && timeInitialized) {
        ESP_LOGW(TAG, "RTC returned invalid time despite being initialized");
        timeInitialized = false;
    }

    return now;
}

struct tm TimeHandler::getTimeStruct() const {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    return timeinfo;
}

void TimeHandler::updateTimeStrings() {
    struct tm timeinfo = getTimeStruct();

    if (timeinfo.tm_year > 120) {  // Year is years since 1900, so 2020 = 120
        // Format into scratch buffers wide enough for any int, then copy: the fixed members are
        // exactly the display width and -Wformat-truncation refuses to write into them directly.
        char dateBuffer[32];
        snprintf(dateBuffer, sizeof(dateBuffer), "%04d/%02d/%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
        strncpy(currentDate, dateBuffer, sizeof(currentDate) - 1);
        currentDate[sizeof(currentDate) - 1] = '\0';

        char timeBuffer[16];
        snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        strncpy(currentTime, timeBuffer, sizeof(currentTime) - 1);
        currentTime[sizeof(currentTime) - 1] = '\0';

        strncpy(dayOfWeek, getDayOfWeekString(timeinfo.tm_wday), sizeof(dayOfWeek) - 1);
        dayOfWeek[sizeof(dayOfWeek) - 1] = '\0';
    }
}

std::string TimeHandler::getCurrentDate() const {
    return std::string(currentDate);
}

std::string TimeHandler::getCurrentTime() const {
    return std::string(currentTime);
}

std::string TimeHandler::getDayOfWeek() const {
    return std::string(dayOfWeek);
}

int TimeHandler::getCurrentYear() const {
    return getTimeStruct().tm_year + 1900;
}

int TimeHandler::getCurrentMonth() const {
    return getTimeStruct().tm_mon + 1;
}

int TimeHandler::getCurrentDay() const {
    return getTimeStruct().tm_mday;
}

int TimeHandler::getCurrentHour() const {
    return getTimeStruct().tm_hour;
}

int TimeHandler::getCurrentMinute() const {
    return getTimeStruct().tm_min;
}

int TimeHandler::getCurrentSecond() const {
    return getTimeStruct().tm_sec;
}

int TimeHandler::getCurrentWeekday() const {
    return getTimeStruct().tm_wday;
}

void TimeHandler::update() {
    const int64_t currentMillis = millis();

    if (!timeInitialized) {
        if (isTimeValid(time(nullptr))) {
            timeInitialized = true;
            syncInFlight = false;
            lastNtpSyncTime = currentMillis;
            updateTimeStrings();
            ESP_LOGI(TAG, "NTP sync successful - Time: %s", currentTime);
        } else if (syncInFlight && currentMillis - syncStartedMs > NTP_SYNC_WARN_MS) {
            ESP_LOGW(TAG, "No NTP answer after 10 seconds; still listening");
            syncInFlight = false;  // SNTP keeps polling on its own; log once
        }
        return;
    }

    // Update time strings periodically
    if (currentMillis - lastTimeCheckTime > TIME_CHECK_INTERVAL) {
        updateTimeStrings();
        lastTimeCheckTime = currentMillis;
    }

    // Re-sync with NTP periodically (non-blocking)
    if (currentMillis - lastNtpSyncTime > NTP_SYNC_INTERVAL) {
        lastNtpSyncTime = currentMillis;
        startSync();
    }
}
