#include "TimeHandler.h"

// Initialize static RTC memory variable
RTC_DATA_ATTR bool TimeHandler::rtcTimeInitialized = false;

TimeHandler::TimeHandler(int timeZone)
  : timeZone(timeZone), lastNtpSyncTime(0), lastTimeCheckTime(0) {
  
  // Configure timezone and NTP servers
  configTime(timeZone * 3600, 0, "pool.ntp.org", "time.nist.gov");
  
  // Upon wake-up from deep sleep, check if RTC has valid time
  time_t now = time(nullptr);
  
  if (isTimeValid(now) || rtcTimeInitialized) {
    Serial.println("TimeHandler: Valid time detected in RTC memory");
    timeInitialized = true;
    rtcTimeInitialized = true;
    
    // Update string representations immediately
    updateTimeStrings();
  } else {
    Serial.println("TimeHandler: RTC time not initialized yet");
    timeInitialized = false;
    rtcTimeInitialized = false;
  }
}

TimeHandler::~TimeHandler() {
  // Nothing to clean up
}

bool TimeHandler::isTimeValid(time_t timeValue) const {
  // Time is valid if it's after Jan 1, 2020
  return timeValue > VALID_TIME_THRESHOLD;
}

String TimeHandler::getDayOfWeekString(int wday) {
  // Convert time.h weekday (0-6, Sunday = 0) to our 3-letter format
  static const char* days[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
  
  if (wday >= 0 && wday <= 6) {
    return days[wday];
  } else {
    return "---";
  }
}

bool TimeHandler::syncWithNTP() {
  Serial.println("TimeHandler: Syncing with NTP server...");
  
  // Request time sync
  sntp_restart();
  
  // Wait up to 5 seconds for time to sync
  unsigned long startAttempt = millis();
  while (millis() - startAttempt < 5000) {
    // Get current time
    time_t now = time(nullptr);
    
    // Check if time is valid (greater than Jan 1, 2020)
    if (isTimeValid(now)) {
      timeInitialized = true;
      rtcTimeInitialized = true;  // Store in RTC memory for persistence across deep sleep
      
      // Update string representations
      updateTimeStrings();
      
      Serial.println("TimeHandler: NTP sync successful");
      lastNtpSyncTime = millis();
      return true;
    }
    
    delay(100);
  }
  
  Serial.println("TimeHandler: NTP sync failed after 5 seconds");
  return false;
}

time_t TimeHandler::getCurrentEpochTime() {
  time_t now = time(nullptr);
  
  // Double-check time validity - handles edge case where RTC 
  // returns invalid time despite being previously initialized
  if (!isTimeValid(now) && timeInitialized) {
    Serial.println("WARNING: RTC returned invalid time despite being initialized");
    timeInitialized = false;  // Reset flag to force a resync
    rtcTimeInitialized = false;
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
  
  // Only update if we have a valid time
  if (timeinfo.tm_year > 120) { // Year is years since 1900, so 2020 = 120
    // Format date as YYYY/MM/DD
    char dateBuffer[11];
    sprintf(dateBuffer, "%04d/%02d/%02d", 
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    currentDate = String(dateBuffer);
    
    // Format time as HH:MM
    char timeBuffer[6];
    sprintf(timeBuffer, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    currentTime = String(timeBuffer);
    
    // Get day of week
    dayOfWeek = getDayOfWeekString(timeinfo.tm_wday);
  }
}

String TimeHandler::getCurrentDate() {
  // Only update strings if time is initialized
  if (timeInitialized) {
    updateTimeStrings();
  }
  return currentDate;
}

String TimeHandler::getCurrentTime() {
  // Only update strings if time is initialized
  if (timeInitialized) {
    updateTimeStrings();
  }
  return currentTime;
}

String TimeHandler::getDayOfWeek() {
  // Only update strings if time is initialized
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
  unsigned long currentMillis = millis();
  
  // Check if it's time to update string representations
  if (timeInitialized && (currentMillis - lastTimeCheckTime > TIME_CHECK_INTERVAL)) {
    updateTimeStrings();
    lastTimeCheckTime = currentMillis;
  }
}