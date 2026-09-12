#pragma once

#include <ctime>
#include <string>
#include "esp_sntp.h"

// Wall clock from SNTP.
//
// NET-07: the UTC offset (minutes, no DST) and the NTP servers are constructor parameters fed
// from Kconfig. NET-10: the date/time/day text lives in fixed character arrays refreshed only
// on the network task; the getters return copies. NET-30: startSync() never waits; update()
// observes when the system clock becomes valid.
class TimeHandler {
private:
    char currentDate[11];   // "YYYY/MM/DD"
    char currentTime[6];    // "HH:MM"
    char dayOfWeek[4];      // "MON"

    const int utcOffsetMinutes_;
    int64_t lastNtpSyncTime;
    int64_t lastTimeCheckTime;
    int64_t syncStartedMs;
    bool timeInitialized;
    bool syncInFlight;

    static constexpr int64_t NTP_SYNC_INTERVAL = 3600000;    // 1 hour (in ms)
    static constexpr int64_t TIME_CHECK_INTERVAL = 1000;     // 1 second (in ms)
    static constexpr int64_t NTP_SYNC_WARN_MS = 10000;       // log once if no answer by then
    static constexpr time_t VALID_TIME_THRESHOLD = 1577836800;  // Jan 1, 2020

    static const char* getDayOfWeekString(int wday);
    void updateTimeStrings();
    bool isTimeValid(time_t timeValue) const;
    int64_t millis() const;

public:
    TimeHandler(int utcOffsetMinutes, const char* server1, const char* server2);
    ~TimeHandler();

    void startSync();                 // non-blocking: init or restart SNTP
    bool syncWithNTP();               // compatibility: starts a sync, returns whether time is valid now
    time_t epochNow() const;          // the system clock, UTC seconds
    time_t getCurrentEpochTime();
    struct tm getTimeStruct() const;
    int utcOffsetMinutes() const { return utcOffsetMinutes_; }

    std::string getCurrentDate() const;
    std::string getCurrentTime() const;
    std::string getDayOfWeek() const;

    int getCurrentYear() const;
    int getCurrentMonth() const;
    int getCurrentDay() const;
    int getCurrentHour() const;
    int getCurrentMinute() const;
    int getCurrentSecond() const;
    int getCurrentWeekday() const;

    bool isTimeInitialized() const { return timeInitialized; }

    void update();
};
