#ifndef INFO_SCREEN_H
#define INFO_SCREEN_H

#include <Arduino_GFX_Library.h>
#include "TouchPanel.h"
#include "WiFiTCPClient.h"

class InfoScreen {
private:
  Arduino_GFX* gfx;
  TouchPanel* touchPanel;
  WiFiTCPClient* tcpClient;

  bool screenInitialized;
  bool pageBackRequested;

  // Date and time
  String currentDate;
  String currentTime;
  String lastDisplayedTime;
  String formattedDate;     // Stores formatted date like "MON 17 MAR"
  String currentDayOfWeek = "---"; // Current day of week (MON, TUE, etc.)

  // For blinking colon
  bool colonVisible = true;
  unsigned long lastColonToggleTime = 0;
  const unsigned long COLON_BLINK_INTERVAL = 1000; // 1 second interval
  int colonX = 0;  // X position of the colon
  int colonY = 0;  // Y position of the colon

  // Temperature data
  float indoorTemp = 18.0;  // Default indoor temperature
  float outdoorTemp = 0.0;  // Default outdoor temperature

  // Previous values to detect changes
  float lastIndoorTemp = 0.0;
  float lastOutdoorTemp = 0.0;
  String lastFormattedDate;
  String lastFormattedTime;

  // Flags for redrawing specific elements
  bool indoorTempChanged = false;
  bool outdoorTempChanged = false;
  bool dateTimeChanged = false;
  bool networkStatusChanged = false;

  // Network status
  bool wifiConnected = false;
  bool internetConnected = false;
  bool mqttConnected = false;
  int wifiStrength = 0; // 0-3 for signal strength

  // Timing for auto mode switching
  unsigned long lastActivityTime;
  unsigned long lastUpdateTime;
  bool inactivityTimeoutReached;

  // Arc configuration
  const int ARC_THICKNESS = 25;         // Thickness for better visibility
  const float ARC_START_ANGLE = 120;    // Defined arc start angle (before rotation)
  const float ARC_LENGTH = 300;         // Defined arc length in degrees
  const float ARC_END_ANGLE = ARC_START_ANGLE + ARC_LENGTH;  // Calculated end angle

  const unsigned long UPDATE_INTERVAL = 5000;    // Update data every 5 seconds
  const unsigned long INACTIVITY_TIMEOUT = 60000;  // 1 minute timeout for sleep

  // Animation state for arcs (for synchronized fill animation)
  float animatedIndoorAngle;
  float animatedOutdoorAngle;
  float targetIndoorAngle;
  float targetOutdoorAngle;
  float startIndoorAngle;
  float startOutdoorAngle;
  unsigned long arcAnimationStartTime;
  const unsigned long ANIMATION_DURATION = 1000; // Animation duration in ms

  // Screen rendering - called internally from update()
  void drawDateTime();
  void drawColonOnly();  // New method to only update the colon
  void drawTemperatureArcs();
  void drawWiFiStatus();

  // Drawing helpers
  void drawWiFiIcon(int x, int y, int size);
  void drawArc(int x, int y, int radius, int thickness, float startAngle, float endAngle, 
               float value, float minValue, float maxValue, uint16_t startColor, uint16_t endColor, 
               bool showTemp, const String& label, float fillAngle);
  uint16_t getTemperatureColor(float temperature, bool isIndoor);
  String getMonthName(int month);

  // Network status update - called internally
  void updateNetworkStatus();

  // Format date as "MON 17 MAR"
  void formatDate();

public:
  InfoScreen(Arduino_GFX* graphics, TouchPanel* touch, WiFiTCPClient* client);

  // Main update method - collects data and updates screen
  void update();

  void setTCPClient(WiFiTCPClient* client);
  void updateDateTime(const String& date, const String& time);
  void updateIndoorTemperature(float temperature);
  void updateOutdoorTemperature(const String& temperature);

  void resetLastActivityTime();
  bool isInactivityTimeoutReached();

  bool isPageBackRequested();
  void resetPageBackRequest();
  void resetScreen(); // Reset screen initialization flag

  // Debug method to help diagnose touch issues
  void printTouchDebugInfo();
};

#endif // INFO_SCREEN_H