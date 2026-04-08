#ifndef TIME_HANDLER_H
#define TIME_HANDLER_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_sntp.h"
#include <time.h>
#include <sys/time.h>

class TimeHandler {
private:
    // RTC memory variable that persists across deep sleep cycles
    RTC_DATA_ATTR static bool rtcTimeInitialized;
    
    String currentDate = "----/--/--"; // Current date (default value)
    String currentTime = "--:--";      // Current time (default value)
    String dayOfWeek = "---";          // Current day of week (default value)
    
    unsigned long lastNtpSyncTime = 0; // Last time we synced with NTP
    unsigned long lastTimeCheckTime = 0; // Last time we updated our string representations
    const int timeZone;                // Timezone offset in hours
    bool timeInitialized = false;      // Flag to indicate if time has been set
    
    // Configuration values
    static const unsigned long NTP_SYNC_INTERVAL = 3600000; // 1 hour (in ms)
    static const unsigned long TIME_CHECK_INTERVAL = 1000;  // 5 seconds (in ms)
    static const time_t VALID_TIME_THRESHOLD = 1577836800;  // Jan 1, 2020 (valid time must be after this)
    
    // Helper method to convert numeric weekday to 3-letter string
    String getDayOfWeekString(int wday);
    
    // Update the string representations of date and time
    void updateTimeStrings();
    
    // Check if a given time is valid (after Jan 1, 2020)
    bool isTimeValid(time_t timeValue) const;

public:
    /**
     * Constructor
     * 
     * @param timeZone Timezone offset in hours from GMT (default: 9 for JST)
     */
    TimeHandler(int timeZone = 9);
    
    /**
     * Destructor
     */
    ~TimeHandler();

    /**
     * Synchronize RTC with NTP servers
     * 
     * @return True if sync was successful, false otherwise
     */
    bool syncWithNTP();
    
    /**
     * Get current epoch time (seconds since 1970-01-01)
     * 
     * @return Current epoch time
     */
    time_t getCurrentEpochTime();
    
    /**
     * Get current time as a struct tm
     * 
     * @return Current time in struct tm format
     */
    struct tm getTimeStruct();
    
    /**
     * Get current date in format YYYY/MM/DD
     * 
     * @return Current date string
     */
    String getCurrentDate();
    
    /**
     * Get current time in format HH:MM
     * 
     * @return Current time string
     */
    String getCurrentTime();
    
    /**
     * Get current day of week (SUN, MON, TUE, etc.)
     * 
     * @return Current day of week string
     */
    String getDayOfWeek();
    
    /**
     * Get current year (4-digit)
     * 
     * @return Current year
     */
    int getCurrentYear();
    
    /**
     * Get current month (1-12)
     * 
     * @return Current month
     */
    int getCurrentMonth();
    
    /**
     * Get current day of month (1-31)
     * 
     * @return Current day of month
     */
    int getCurrentDay();
    
    /**
     * Get current hour (0-23)
     * 
     * @return Current hour
     */
    int getCurrentHour();
    
    /**
     * Get current minute (0-59)
     * 
     * @return Current minute
     */
    int getCurrentMinute();
    
    /**
     * Get current second (0-59)
     * 
     * @return Current second
     */
    int getCurrentSecond();
    
    /**
     * Get current weekday (0-6, where 0=Sunday)
     * 
     * @return Current weekday
     */
    int getCurrentWeekday();
    
    /**
     * Check if time has been initialized
     * 
     * @return True if time is valid and initialized
     */
    bool isTimeInitialized() const { return timeInitialized; }
    
    /**
     * Periodic update method - call this regularly
     * Will update time strings and check if NTP sync is needed
     */
    void update();
};

#endif // TIME_HANDLER_H