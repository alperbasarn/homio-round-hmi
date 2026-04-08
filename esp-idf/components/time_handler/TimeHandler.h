#pragma once

#include <string>
#include <ctime>
#include "esp_sntp.h"

class TimeHandler {
private:
    std::string currentDate;
    std::string currentTime;
    std::string dayOfWeek;

    const int timeZone;
    int64_t lastNtpSyncTime;
    int64_t lastTimeCheckTime;
    bool timeInitialized;

    static constexpr int64_t NTP_SYNC_INTERVAL = 3600000;    // 1 hour (in ms)
    static constexpr int64_t TIME_CHECK_INTERVAL = 1000;     // 1 second (in ms)
    static constexpr time_t VALID_TIME_THRESHOLD = 1577836800;  // Jan 1, 2020

    std::string getDayOfWeekString(int wday);
    void updateTimeStrings();
    bool isTimeValid(time_t timeValue) const;
    int64_t millis() const;

public:
    TimeHandler(int timeZone = 3);  // Default to Turkey timezone (GMT+3)
    ~TimeHandler();

    bool syncWithNTP();
    time_t getCurrentEpochTime();
    struct tm getTimeStruct();

    std::string getCurrentDate();
    std::string getCurrentTime();
    std::string getDayOfWeek();

    int getCurrentYear();
    int getCurrentMonth();
    int getCurrentDay();
    int getCurrentHour();
    int getCurrentMinute();
    int getCurrentSecond();
    int getCurrentWeekday();

    bool isTimeInitialized() const { return timeInitialized; }

    void update();
};
