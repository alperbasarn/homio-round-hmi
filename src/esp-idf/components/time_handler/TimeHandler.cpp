#include "TimeHandler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "TimeHandler";
static constexpr const char* DEFAULT_DATE = "1994/03/11";
static constexpr const char* DEFAULT_TIME = "04:30";
static constexpr const char* DEFAULT_DAY_OF_WEEK = "FRI";

TimeHandler::TimeHandler(int timeZone)
    : timeZone(timeZone), lastNtpSyncTime(0), lastTimeCheckTime(0), timeInitialized(false) {

    currentDate = DEFAULT_DATE;
    currentTime = DEFAULT_TIME;
    dayOfWeek = DEFAULT_DAY_OF_WEEK;

    // Configure timezone
    char tzStr[32];
    if (timeZone >= 0) {
        snprintf(tzStr, sizeof(tzStr), "UTC-%d", timeZone);
    } else {
        snprintf(tzStr, sizeof(tzStr), "UTC+%d", -timeZone);
    }
    setenv("TZ", tzStr, 1);
    tzset();

    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");

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

std::string TimeHandler::getDayOfWeekString(int wday) {
    static const char* days[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    if (wday >= 0 && wday <= 6) {
        return std::string(days[wday]);
    }
    return "---";
}

bool TimeHandler::syncWithNTP() {
    ESP_LOGI(TAG, "Syncing with NTP server...");

    // Initialize SNTP if not already running
    if (esp_sntp_enabled() == false) {
        esp_sntp_init();
    } else {
        esp_sntp_restart();
    }

    // Wait up to 10 seconds for time to sync
    int64_t startAttempt = millis();
    while (millis() - startAttempt < 10000) {
        time_t now = time(nullptr);

        if (isTimeValid(now)) {
            timeInitialized = true;
            updateTimeStrings();
            ESP_LOGI(TAG, "NTP sync successful - Time: %s", currentTime.c_str());
            lastNtpSyncTime = millis();
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGW(TAG, "NTP sync failed after 10 seconds");
    return false;
}

time_t TimeHandler::getCurrentEpochTime() {
    time_t now = time(nullptr);

    if (!isTimeValid(now) && timeInitialized) {
        ESP_LOGW(TAG, "RTC returned invalid time despite being initialized");
        timeInitialized = false;
    }

    return now;
}

struct tm TimeHandler::getTimeStruct() {
    time_t now = getCurrentEpochTime();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    return timeinfo;
}

void TimeHandler::updateTimeStrings() {
    struct tm timeinfo = getTimeStruct();

    if (timeinfo.tm_year > 120) {  // Year is years since 1900, so 2020 = 120
        char dateBuffer[32];
        snprintf(dateBuffer, sizeof(dateBuffer), "%04d/%02d/%02d",
                timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
        currentDate = std::string(dateBuffer);

        char timeBuffer[16];
        snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        currentTime = std::string(timeBuffer);

        dayOfWeek = getDayOfWeekString(timeinfo.tm_wday);
    }
}

std::string TimeHandler::getCurrentDate() {
    if (timeInitialized) {
        updateTimeStrings();
    }
    return currentDate;
}

std::string TimeHandler::getCurrentTime() {
    if (timeInitialized) {
        updateTimeStrings();
    }
    return currentTime;
}

std::string TimeHandler::getDayOfWeek() {
    if (timeInitialized) {
        updateTimeStrings();
    }
    return dayOfWeek;
}

int TimeHandler::getCurrentYear() {
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_year + 1900;
}

int TimeHandler::getCurrentMonth() {
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_mon + 1;
}

int TimeHandler::getCurrentDay() {
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_mday;
}

int TimeHandler::getCurrentHour() {
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_hour;
}

int TimeHandler::getCurrentMinute() {
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_min;
}

int TimeHandler::getCurrentSecond() {
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_sec;
}

int TimeHandler::getCurrentWeekday() {
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_wday;
}

void TimeHandler::update() {
    int64_t currentMillis = millis();

    // Update time strings periodically
    if (timeInitialized && (currentMillis - lastTimeCheckTime > TIME_CHECK_INTERVAL)) {
        updateTimeStrings();
        lastTimeCheckTime = currentMillis;
    }

    // Re-sync with NTP periodically
    if (timeInitialized && (currentMillis - lastNtpSyncTime > NTP_SYNC_INTERVAL)) {
        syncWithNTP();
    }
}
