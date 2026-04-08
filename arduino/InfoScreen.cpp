#include "InfoScreen.h"
#include <math.h>

InfoScreen::InfoScreen(Arduino_GFX* graphics, TouchPanel* touch, WiFiTCPClient* client)
  : gfx(graphics), touchPanel(touch), tcpClient(client),
    screenInitialized(false), pageBackRequested(false),
    currentDate("--/--"), currentTime("--:--"), lastDisplayedTime(""),
    lastUpdateTime(0), lastActivityTime(0), inactivityTimeoutReached(false) {

  // Initialize animation state.
  // We use the final target angles immediately at startup.
  float effectiveStart = fmod(ARC_START_ANGLE + 180.0, 360.0);
  animatedIndoorAngle = effectiveStart;
  animatedOutdoorAngle = effectiveStart;
  targetIndoorAngle = effectiveStart;
  targetOutdoorAngle = effectiveStart;
  startIndoorAngle = effectiveStart;
  startOutdoorAngle = effectiveStart;
  arcAnimationStartTime = millis();

  // Initialize colon blinking
  lastColonToggleTime = millis();
  colonVisible = true;
  colonX = 0;  // Will be set when time is first drawn
  colonY = 0;  // Will be set when time is first drawn
}

void InfoScreen::setTCPClient(WiFiTCPClient* client) {
  tcpClient = client;
}

void InfoScreen::update() {
  unsigned long currentMillis = millis();
  // Fetch and update time
  String newDate = tcpClient->getCurrentDate();
  String newTime = tcpClient->getCurrentTime();
  String newDayOfWeek = tcpClient->getDayOfWeek();  // Get day of week from client

  // Check if time, date, or day of week has changed
  if (newDate != currentDate || newTime != currentTime || newDayOfWeek != currentDayOfWeek) {
    currentDate = newDate;
    currentTime = newTime;
    currentDayOfWeek = newDayOfWeek;
    formatDate();

    if (formattedDate != lastFormattedDate || currentTime != lastFormattedTime) {
      lastFormattedDate = formattedDate;
      lastFormattedTime = currentTime;
      dateTimeChanged = true;
    }
  }

  // Check if we need to update just the colon
  if (currentMillis - lastColonToggleTime >= COLON_BLINK_INTERVAL) {
    colonVisible = !colonVisible;
    lastColonToggleTime = currentMillis;

    // Only update the colon, not the entire time display
    if (colonX > 0) {  // Make sure we have valid coordinates
      drawColonOnly();
    } else {
      // If we don't have colon coordinates yet, we need to redraw the whole time
      dateTimeChanged = true;
    }
  }

  // Reset change flags
  indoorTempChanged = false;
  outdoorTempChanged = false;
  networkStatusChanged = false;
  // Note: We don't reset dateTimeChanged here as it might have been set previously

  // Update network status
  updateNetworkStatus();

/*
  // Check for inactivity timeout
  if (currentMillis - lastActivityTime >= INACTIVITY_TIMEOUT) {
    inactivityTimeoutReached = true;
    Serial.println("InfoScreen: Inactivity timeout reached");
  }
*/
  // Update time and temperature data if internet is available and it's time to update
  if (tcpClient && tcpClient->isInternetAvailable() && (currentMillis - lastUpdateTime >= UPDATE_INTERVAL)) {
    // Fetch outdoor temperature
    String newWeatherTemp = tcpClient->getWeatherTemperature();
    // Parse temperature value
    float newOutdoorTemp = 0.0f;
    int tempEnd = newWeatherTemp.indexOf('°');
    if (tempEnd > 0) {
      newOutdoorTemp = newWeatherTemp.substring(0, tempEnd).toFloat();
    }

    // Check if outdoor temperature has changed
    if (abs(newOutdoorTemp - outdoorTemp) > 0.5f) {
      outdoorTemp = newOutdoorTemp;
      outdoorTempChanged = true;
    }

    lastUpdateTime = currentMillis;
  }

  // Full screen redraw on initialization
  if (!screenInitialized) {
    gfx->fillScreen(BLACK);
    // On first startup, bypass animation (draw final arc lengths)
    drawDateTime();
    drawWiFiStatus();
    drawTemperatureArcs();
    screenInitialized = true;
  } else {
    // Update only those elements that changed
    if (indoorTempChanged || outdoorTempChanged) {
      drawTemperatureArcs();
    }
    if (dateTimeChanged) {
      drawDateTime();
      dateTimeChanged = false;  // Reset flag after drawing
    }
    if (networkStatusChanged) {
      drawWiFiStatus();
    }
  }

  // Check for touch events
  if (touchPanel && touchPanel->getHasNewPress()) {
    resetLastActivityTime();
    pageBackRequested = true;
    screenInitialized = false;  // Force a full redraw
    Serial.println("InfoScreen: Touch detected, setting pageBackRequested");
  }
}

void InfoScreen::formatDate() {
  // Parse YYYY/MM/DD format
  int firstSlash = currentDate.indexOf('/');
  int secondSlash = currentDate.indexOf('/', firstSlash + 1);

  if (firstSlash > 0 && secondSlash > 0) {
    String yearStr = currentDate.substring(0, firstSlash);
    String monthStr = currentDate.substring(firstSlash + 1, secondSlash);
    String dayStr = currentDate.substring(secondSlash + 1);

    // Remove leading zeros from day
    while (dayStr.length() > 1 && dayStr[0] == '0') {
      dayStr = dayStr.substring(1);
    }

    // Get month name
    int monthNum = monthStr.toInt();
    String monthName = getMonthName(monthNum);

    // Format as "17 MONTH" - e.g., "17 MAR" (day of week will be displayed separately)
    formattedDate = dayStr + " " + monthName;
  } else {
    formattedDate = currentDate;  // Fallback if date format is unexpected
  }
}

String InfoScreen::getMonthName(int month) {
  const char* months[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                           "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

  if (month >= 1 && month <= 12) {
    return months[month - 1];
  }
  return "---";
}

// New method to only draw the colon part of the time
void InfoScreen::drawColonOnly() {
  // We only need to draw a small area where the colon is
  int colonWidth = 10;   // Width of the colon area
  int colonHeight = 18;  // Height of the colon area (for text size 3)

  // Black out the colon area
  gfx->fillRect(colonX, colonY, colonWidth, colonHeight, BLACK);

  // If colon should be visible, redraw it
  if (colonVisible) {
    gfx->setTextColor(WHITE);
    gfx->setTextSize(3);
    gfx->setCursor(colonX, colonY);
    gfx->print(":");
  }
}

// Updated to implement blinking colon and track colon position
void InfoScreen::drawDateTime() {
  int centerX = gfx->width() / 2;
  int centerY = gfx->height() / 2;

  // Calculate required radius for our text content
  // This should be enough to contain all three lines of text without overlapping the arcs
  int clearRadius = 55;  // Adjust based on your testing

  // Clear a circular area in the center instead of a rectangle
  // This avoids drawing over the temperature arcs
  gfx->fillCircle(centerX, centerY, clearRadius, BLACK);

  // Draw time at the top, large size
  gfx->setTextColor(WHITE);
  gfx->setTextSize(3);

  // Find the colon position in the time string
  int colonPos = currentTime.indexOf(':');
  String hours = "";
  String minutes = "";

  if (colonPos > 0) {
    hours = currentTime.substring(0, colonPos);
    minutes = currentTime.substring(colonPos + 1);
  } else {
    // Fallback if no colon is found
    hours = currentTime;
    minutes = "";
  }

  // Calculate the total width to center it
  int timeWidth = (hours.length() + minutes.length() + 1) * 18;  // +1 for colon, 18 pixels per character at size 3
  int timeX = centerX - timeWidth / 2;
  int timeY = centerY - 30;

  // Draw hours
  gfx->setCursor(timeX, timeY);
  gfx->print(hours);

  // Save the position right after hours for the colon
  colonX = timeX + hours.length() * 18;
  colonY = timeY;

  // Draw colon if visible
  if (colonVisible) {
    gfx->setCursor(colonX, colonY);
    gfx->print(":");
  }

  // Draw minutes
  gfx->setCursor(colonX + 18, timeY);  // 18 pixels for the colon space
  gfx->print(minutes);

  // Draw day of week in the middle
  gfx->setTextSize(2);
  int dayWidth = currentDayOfWeek.length() * 12;  // Approximate width for size 2 text
  gfx->setCursor(centerX - dayWidth / 2, centerY);
  gfx->print(currentDayOfWeek);

  // Draw date at the bottom
  gfx->setTextSize(2);
  int dateWidth = formattedDate.length() * 12;  // Approximate width for size 2 text
  gfx->setCursor(centerX - dateWidth / 2, centerY + 25);
  gfx->print(formattedDate);
}

void InfoScreen::drawTemperatureArcs() {
  int centerX = gfx->width() / 2;
  int centerY = gfx->height() / 2;
  int spacing = 5;  // Spacing between arcs

  // Calculate radii for indoor and outdoor arcs
  int indoorOuterRadius = min(gfx->width(), gfx->height()) / 2;
  int indoorInnerRadius = indoorOuterRadius - ARC_THICKNESS;
  int outdoorOuterRadius = indoorInnerRadius - spacing;
  int outdoorInnerRadius = outdoorOuterRadius - ARC_THICKNESS;

  // Compute effective angles by rotating by 180° (consistent for all draws)
  float effectiveStart = fmod(ARC_START_ANGLE + 180.0f, 360.0f);
  float effectiveEnd = fmod(ARC_END_ANGLE + 180.0f, 360.0f);
  if (effectiveEnd < effectiveStart) {
    effectiveEnd += 360.0f;
  }
  float arcSpan = effectiveEnd - effectiveStart;

  // Compute target fill angles based on temperature values
  float normalizedIndoor = constrain((indoorTemp - 0.0f) / 40.0f, 0.0f, 1.0f);
  float newTargetIndoor = effectiveStart + normalizedIndoor * arcSpan;
  float normalizedOutdoor = constrain((outdoorTemp - (-20.0f)) / 70.0f, 0.0f, 1.0f);
  float newTargetOutdoor = effectiveStart + normalizedOutdoor * arcSpan;

  unsigned long currentMillis = millis();

  // On first startup or if any temperature changes, reset animation
  if (!screenInitialized || indoorTempChanged || outdoorTempChanged) {
    // For initial display, we can bypass animation
    if (!screenInitialized) {
      animatedIndoorAngle = newTargetIndoor;
      animatedOutdoorAngle = newTargetOutdoor;
    } else {
      // Not initial display, so we need to animate
      // Both animations start from minimum and grow together
      startIndoorAngle = effectiveStart;
      startOutdoorAngle = effectiveStart;
      // Reset animation time
      arcAnimationStartTime = currentMillis;
    }

    // Update target angles regardless
    targetIndoorAngle = newTargetIndoor;
    targetOutdoorAngle = newTargetOutdoor;
  }

  // Calculate animation progress
  float progress = constrain((float)(currentMillis - arcAnimationStartTime) / (float)ANIMATION_DURATION, 0.0f, 1.0f);

  // Apply the same progress to both arcs - this ensures synchronized growth
  animatedIndoorAngle = startIndoorAngle + (targetIndoorAngle - startIndoorAngle) * progress;
  animatedOutdoorAngle = startOutdoorAngle + (targetOutdoorAngle - startOutdoorAngle) * progress;

  // Get colors for arcs
  uint16_t indoorColor = getTemperatureColor(indoorTemp, true);
  uint16_t outdoorColor = getTemperatureColor(outdoorTemp, false);
  uint16_t bgColor = gfx->color565(30, 30, 30);

  // Calculate the cap radius as half the arc thickness
  int capRadius = ARC_THICKNESS / 2;
  
  // Calculate start cap positions (static)
  float startRad = effectiveStart * PI / 180.0f;
  int indoorStartX = centerX - indoorOuterRadius * cos(startRad) + capRadius * cos(startRad);
  int indoorStartY = centerY - indoorOuterRadius * sin(startRad) + capRadius * sin(startRad);
  int outdoorStartX = centerX - outdoorOuterRadius * cos(startRad) + capRadius * cos(startRad);
  int outdoorStartY = centerY - outdoorOuterRadius * sin(startRad) + capRadius * sin(startRad);
  
  // Draw static start caps first
  gfx->fillCircle(indoorStartX, indoorStartY, capRadius, indoorColor);
  gfx->fillCircle(outdoorStartX, outdoorStartY, capRadius, outdoorColor);

  // Use a smaller angle step for smoother arcs
  const float angleStep = 0.5f;

  // Draw both arcs using filled triangles
  for (float angle = effectiveStart; angle < effectiveEnd; angle += angleStep) {
    float nextAngle = constrain(angle + angleStep, angle, effectiveEnd);

    // Convert angles to radians
    float rad1 = angle * PI / 180.0f;
    float rad2 = nextAngle * PI / 180.0f;

    // Calculate points for indoor arc segment
    int x1Inner = centerX - indoorInnerRadius * cos(rad1);
    int y1Inner = centerY - indoorInnerRadius * sin(rad1);
    int x2Inner = centerX - indoorInnerRadius * cos(rad2);
    int y2Inner = centerY - indoorInnerRadius * sin(rad2);

    int x1Outer = centerX - indoorOuterRadius * cos(rad1);
    int y1Outer = centerY - indoorOuterRadius * sin(rad1);
    int x2Outer = centerX - indoorOuterRadius * cos(rad2);
    int y2Outer = centerY - indoorOuterRadius * sin(rad2);

    // Draw indoor arc segment (filled quadrilateral) if within the animated range
    uint16_t indoorPixelColor = (angle < animatedIndoorAngle) ? indoorColor : bgColor;
    gfx->fillTriangle(x1Inner, y1Inner, x1Outer, y1Outer, x2Outer, y2Outer, indoorPixelColor);
    gfx->fillTriangle(x1Inner, y1Inner, x2Inner, y2Inner, x2Outer, y2Outer, indoorPixelColor);

    // Calculate points for outdoor arc segment
    x1Inner = centerX - outdoorInnerRadius * cos(rad1);
    y1Inner = centerY - outdoorInnerRadius * sin(rad1);
    x2Inner = centerX - outdoorInnerRadius * cos(rad2);
    y2Inner = centerY - outdoorInnerRadius * sin(rad2);

    x1Outer = centerX - outdoorOuterRadius * cos(rad1);
    y1Outer = centerY - outdoorOuterRadius * sin(rad1);
    x2Outer = centerX - outdoorOuterRadius * cos(rad2);
    y2Outer = centerY - outdoorOuterRadius * sin(rad2);

    // Draw outdoor arc segment (filled quadrilateral) if within the animated range
    uint16_t outdoorPixelColor = (angle < animatedOutdoorAngle) ? outdoorColor : bgColor;
    gfx->fillTriangle(x1Inner, y1Inner, x1Outer, y1Outer, x2Outer, y2Outer, outdoorPixelColor);
    gfx->fillTriangle(x1Inner, y1Inner, x2Inner, y2Inner, x2Outer, y2Outer, outdoorPixelColor);
  }
  
  // Calculate and draw dynamic end caps (moving with animation)
  float indoorEndRad = animatedIndoorAngle * PI / 180.0f;
  float outdoorEndRad = animatedOutdoorAngle * PI / 180.0f;
  
  // Position the end caps at the middle of the arc thickness
  int indoorEndX = centerX - (indoorInnerRadius + indoorOuterRadius)/2 * cos(indoorEndRad);
  int indoorEndY = centerY - (indoorInnerRadius + indoorOuterRadius)/2 * sin(indoorEndRad);
  int outdoorEndX = centerX - (outdoorInnerRadius + outdoorOuterRadius)/2 * cos(outdoorEndRad);
  int outdoorEndY = centerY - (outdoorInnerRadius + outdoorOuterRadius)/2 * sin(outdoorEndRad);
  
  // Draw the dynamic end caps
  gfx->fillCircle(indoorEndX, indoorEndY, capRadius, indoorColor);
  gfx->fillCircle(outdoorEndX, outdoorEndY, capRadius, outdoorColor);

  // Display temperature labels with DOUBLED text size
  gfx->setTextColor(WHITE);
  gfx->setTextSize(3);  // Doubled from 1 to 2

  // Indoor temperature label
  float midAngle = (effectiveStart + effectiveEnd) / 2.0f;
  float midRad = midAngle * PI / 180.0f;
  int textX = centerX;
  // Adjust Y position to account for larger text
  int textY = centerY - (indoorInnerRadius + ARC_THICKNESS / 2) * sin(midRad);
  String indoorText = String(int(round(indoorTemp)));
  int textWidth = indoorText.length() * 12;           // Width for text
  gfx->setCursor(textX - textWidth / 2, textY - 12);  // Adjusted Y offset for larger text
  gfx->print(indoorText);

  // Outdoor temperature label
  textY = centerY - (outdoorInnerRadius + ARC_THICKNESS / 2) * sin(midRad);
  String outdoorText = String(int(round(outdoorTemp)));
  textWidth = outdoorText.length() * 12;              // Width for text
  gfx->setCursor(textX - textWidth / 2, textY - 12);  // Adjusted Y offset for larger text
  gfx->print(outdoorText);
}

/*
void InfoScreen::drawTemperatureArcs() {
  int centerX = gfx->width() / 2;
  int centerY = gfx->height() / 2;
  int spacing = 5;  // Increased spacing between arcs

  // Calculate radii for indoor and outdoor arcs
  int indoorOuterRadius = min(gfx->width(), gfx->height()) / 2;
  int indoorInnerRadius = indoorOuterRadius - ARC_THICKNESS;
  int outdoorOuterRadius = indoorInnerRadius - spacing;
  int outdoorInnerRadius = outdoorOuterRadius - ARC_THICKNESS;

  // Compute effective angles by rotating by 180° (consistent for all draws)
  float effectiveStart = fmod(ARC_START_ANGLE + 180.0f, 360.0f);
  float effectiveEnd = fmod(ARC_END_ANGLE + 180.0f, 360.0f);
  if (effectiveEnd < effectiveStart) {
    effectiveEnd += 360.0f;
  }
  float arcSpan = effectiveEnd - effectiveStart;

  // Compute target fill angles based on temperature values
  float normalizedIndoor = constrain((indoorTemp - 0.0f) / 40.0f, 0.0f, 1.0f);
  float newTargetIndoor = effectiveStart + normalizedIndoor * arcSpan;
  float normalizedOutdoor = constrain((outdoorTemp - (-20.0f)) / 70.0f, 0.0f, 1.0f);
  float newTargetOutdoor = effectiveStart + normalizedOutdoor * arcSpan;

  unsigned long currentMillis = millis();

  // On first startup or if any temperature changes, reset animation
  if (!screenInitialized || indoorTempChanged || outdoorTempChanged) {
    // For initial display, we can bypass animation
    if (!screenInitialized) {
      animatedIndoorAngle = newTargetIndoor;
      animatedOutdoorAngle = newTargetOutdoor;
    } else {
      // Not initial display, so we need to animate
      // Both animations start from minimum and grow together
      startIndoorAngle = effectiveStart;
      startOutdoorAngle = effectiveStart;
      // Reset animation time
      arcAnimationStartTime = currentMillis;
    }

    // Update target angles regardless
    targetIndoorAngle = newTargetIndoor;
    targetOutdoorAngle = newTargetOutdoor;
  }

  // Calculate animation progress
  float progress = constrain((float)(currentMillis - arcAnimationStartTime) / (float)ANIMATION_DURATION, 0.0f, 1.0f);

  // Apply the same progress to both arcs - this ensures synchronized growth
  animatedIndoorAngle = startIndoorAngle + (targetIndoorAngle - startIndoorAngle) * progress;
  animatedOutdoorAngle = startOutdoorAngle + (targetOutdoorAngle - startOutdoorAngle) * progress;

  // Get colors for arcs with updated method signature
  uint16_t indoorColor = getTemperatureColor(indoorTemp, true);     // true for indoor
  uint16_t outdoorColor = getTemperatureColor(outdoorTemp, false);  // false for outdoor
  uint16_t bgColor = gfx->color565(30, 30, 30);

  // Use a smaller angle step for smoother arcs
  const float angleStep = 0.5f;

  // Draw both arcs using filled triangles to avoid the lines to center
  for (float angle = effectiveStart; angle < effectiveEnd; angle += angleStep) {
    float nextAngle = constrain(angle + angleStep, angle, effectiveEnd);

    // Convert angles to radians
    float rad1 = angle * PI / 180.0f;
    float rad2 = nextAngle * PI / 180.0f;

    // Calculate points for indoor arc segment
    int x1Inner = centerX - indoorInnerRadius * cos(rad1);
    int y1Inner = centerY - indoorInnerRadius * sin(rad1);
    int x2Inner = centerX - indoorInnerRadius * cos(rad2);
    int y2Inner = centerY - indoorInnerRadius * sin(rad2);

    int x1Outer = centerX - indoorOuterRadius * cos(rad1);
    int y1Outer = centerY - indoorOuterRadius * sin(rad1);
    int x2Outer = centerX - indoorOuterRadius * cos(rad2);
    int y2Outer = centerY - indoorOuterRadius * sin(rad2);
    // Draw indoor arc segment (filled quadrilateral)
    uint16_t indoorPixelColor = (angle < animatedIndoorAngle) ? indoorColor : bgColor;
    gfx->fillTriangle(x1Inner, y1Inner, x1Outer, y1Outer, x2Outer, y2Outer, indoorPixelColor);
    gfx->fillTriangle(x1Inner, y1Inner, x2Inner, y2Inner, x2Outer, y2Outer, indoorPixelColor);

    // Calculate points for outdoor arc segment
    x1Inner = centerX - outdoorInnerRadius * cos(rad1);
    y1Inner = centerY - outdoorInnerRadius * sin(rad1);
    x2Inner = centerX - outdoorInnerRadius * cos(rad2);
    y2Inner = centerY - outdoorInnerRadius * sin(rad2);

    x1Outer = centerX - outdoorOuterRadius * cos(rad1);
    y1Outer = centerY - outdoorOuterRadius * sin(rad1);
    x2Outer = centerX - outdoorOuterRadius * cos(rad2);
    y2Outer = centerY - outdoorOuterRadius * sin(rad2);

    // Draw outdoor arc segment (filled quadrilateral)
    uint16_t outdoorPixelColor = (angle < animatedOutdoorAngle) ? outdoorColor : bgColor;
    gfx->fillTriangle(x1Inner, y1Inner, x1Outer, y1Outer, x2Outer, y2Outer, outdoorPixelColor);
    gfx->fillTriangle(x1Inner, y1Inner, x2Inner, y2Inner, x2Outer, y2Outer, outdoorPixelColor);
  }

  // Display temperature labels with DOUBLED text size
  gfx->setTextColor(WHITE);
  gfx->setTextSize(3);  // Doubled from 1 to 2

  // Indoor temperature label
  float midAngle = (effectiveStart + effectiveEnd) / 2.0f;
  float midRad = midAngle * PI / 180.0f;
  int textX = centerX;
  // Adjust Y position to account for larger text
  int textY = centerY - (indoorInnerRadius + ARC_THICKNESS / 2) * sin(midRad);
  String indoorText = String(int(round(indoorTemp)));
  int textWidth = indoorText.length() * 12;           // Doubled width for size 2 text
  gfx->setCursor(textX - textWidth / 2, textY - 12);  // Adjusted Y offset for larger text
  gfx->print(indoorText);

  // Outdoor temperature label
  textY = centerY - (outdoorInnerRadius + ARC_THICKNESS / 2) * sin(midRad);
  String outdoorText = String(int(round(outdoorTemp)));
  textWidth = outdoorText.length() * 12;              // Doubled width for size 2 text
  gfx->setCursor(textX - textWidth / 2, textY - 12);  // Adjusted Y offset for larger text
  gfx->print(outdoorText);
}
*/

// Updated getTemperatureColor method with new color ranges and fixed floating point types
uint16_t InfoScreen::getTemperatureColor(float temperature, bool isIndoor) {
  uint8_t r, g, b;

  if (isIndoor) {
    // Indoor temperature color mapping
    if (temperature <= 15.0f) {
      // Light blue gradient for coldest temps (≤15°C)
      float t = temperature / 15.0f;  // 0 to 1
      r = 150 * t;
      g = 150 * t;
      b = 255;
    } else if (temperature <= 18.0f) {
      // Static blue for cold temps (15-18°C)
      r = 0;
      g = 0;
      b = 255;
    } else if (temperature <= 23.0f) {
      // Green gradient for normal temps (19-21°C)
      float t = (temperature - 19.0f) / 2.0f;  // 0 to 1
      t = constrain(t, 0.0f, 1.0f);
      r = 0;
      g = 150 + 105 * t;  // Light green to dark green
      b = 0;
    } else if (temperature <= 26.0f) {
      // Yellow to orange gradient for warm temps (22-26°C)
      float t = (temperature - 24.0f) / 4.0f;  // 0 to 1
      r = 255;
      g = 255 - 140 * t;  // Yellow to orange
      b = 0;
    } else {
      // Red to dark red gradient for hot temps (>26°C)
      float t = constrain((temperature - 26.0f) / 10.0f, 0.0f, 1.0f);  // 0 to 1, capped
      r = 255;
      g = 115 * (1 - t);  // Red to dark red
      b = 0;
    }
  } else {
    // Outdoor temperature color mapping
    if (temperature <= 5.0f) {
      // Light blue gradient for coldest temps (≤5°C)
      float t = (temperature + 5.0f) / 10.0f;  // -5 to 5 mapped to 0 to 1
      t = constrain(t, 0.0f, 1.0f);
      r = 150 * t;
      g = 150 * t;
      b = 255;
    } else if (temperature <= 15.0f) {
      // Static blue for cold temps (5-15°C)
      r = 0;
      g = 0;
      b = 255;
    } else if (temperature <= 24.0f) {
      // Green gradient for normal temps (16-24°C)
      float t = (temperature - 16.0f) / 8.0f;  // 0 to 1
      r = 0;
      g = 150 + 105 * t;  // Light green to dark green
      b = 0;
    } else if (temperature <= 30.0f) {
      // Yellow to orange gradient for warm temps (25-30°C)
      float t = (temperature - 25.0f) / 5.0f;  // 0 to 1
      r = 255;
      g = 255 - 140 * t;  // Yellow to orange
      b = 0;
    } else {
      // Red to dark red gradient for hot temps (>30°C)
      float t = constrain((temperature - 30.0f) / 10.0f, 0.0f, 1.0f);  // 0 to 1, capped
      r = 255;
      g = 115 * (1 - t);  // Red to dark red
      b = 0;
    }
  }

  return gfx->color565(r, g, b);
}

void InfoScreen::drawWiFiStatus() {
  int iconSize = 15;
  int iconX = gfx->width() / 2;
  int iconY = gfx->height() - 20;

  // Clear the area to avoid artifacts.
  gfx->fillRect(iconX - iconSize - 5, iconY - iconSize - 5,
                (iconSize + 5) * 2, (iconSize + 5) * 2, BLACK);
  drawWiFiIcon(iconX, iconY, iconSize);
}

void InfoScreen::drawWiFiIcon(int x, int y, int size) {
  uint16_t wifiColor;
  if (!wifiConnected) {
    wifiColor = gfx->color565(150, 150, 150);
  } else if (internetConnected) {
    wifiColor = gfx->color565(0, 200, 0);
  } else {
    wifiColor = gfx->color565(150, 150, 150);
  }

  // Draw WiFi arcs using the native drawArc (for the icon).
  for (int i = 1; i <= 3; i++) {
    int radius = (size / 3) * i;
    if (wifiConnected && wifiStrength >= i) {
      gfx->drawArc(x, y, radius, radius - 1, 225, 315, wifiColor);
    } else {
      gfx->drawArc(x, y, radius, radius - 1, 225, 315, gfx->color565(60, 60, 60));
    }
  }
  gfx->fillCircle(x, y, 2, wifiColor);
  if (!wifiConnected) {
    gfx->drawLine(x - size / 2, y - size / 2, x + size / 2, y + size / 2, gfx->color565(255, 0, 0));
    gfx->drawLine(x - size / 2 + 1, y - size / 2, x + size / 2 + 1, y + size / 2, gfx->color565(255, 0, 0));
  }
  if (mqttConnected) {
    int tickSize = size / 5;
    gfx->fillTriangle(
      x, y + size / 2 + 5,
      x - tickSize, y + size / 2 + 5 + tickSize,
      x + tickSize, y + size / 2 + 5 + tickSize,
      gfx->color565(0, 255, 0));
  }
}

void InfoScreen::updateNetworkStatus() {
  if (tcpClient) {
    bool newWifiConnected = tcpClient->isWiFiConnected();
    bool newInternetConnected = tcpClient->isInternetAvailable();
    bool newMqttConnected = tcpClient->isMQTTConnected();
    int newWifiStrength = tcpClient->getWiFiSignalStrength();

    if (newWifiConnected != wifiConnected || newInternetConnected != internetConnected || newMqttConnected != mqttConnected || newWifiStrength != wifiStrength) {

      wifiConnected = newWifiConnected;
      internetConnected = newInternetConnected;
      mqttConnected = newMqttConnected;
      wifiStrength = newWifiStrength;

      networkStatusChanged = true;
    }
  }
}

void InfoScreen::updateDateTime(const String& date, const String& time) {
  if (date != currentDate || time != currentTime) {
    currentDate = date;
    currentTime = time;

    // Update day of week whenever date changes
    if (tcpClient) {
      currentDayOfWeek = tcpClient->getDayOfWeek();
    }

    formatDate();

    if (formattedDate != lastFormattedDate || currentTime != lastFormattedTime) {
      lastFormattedDate = formattedDate;
      lastFormattedTime = currentTime;
      dateTimeChanged = true;
      drawDateTime();
    }
  }
}

void InfoScreen::updateIndoorTemperature(float temperature) {
  if (abs(temperature - indoorTemp) > 0.5f) {
    indoorTemp = temperature;
    indoorTempChanged = true;
    drawTemperatureArcs();
  }
}

void InfoScreen::updateOutdoorTemperature(const String& temperature) {
  float newTemp = 0.0f;
  int tempEnd = temperature.indexOf('°');
  if (tempEnd > 0) {
    newTemp = temperature.substring(0, tempEnd).toFloat();
  }
  if (abs(newTemp - outdoorTemp) > 0.5f) {
    outdoorTemp = newTemp;
    outdoorTempChanged = true;
    drawTemperatureArcs();
  }
}

void InfoScreen::resetLastActivityTime() {
  lastActivityTime = millis();
  inactivityTimeoutReached = false;
}

bool InfoScreen::isInactivityTimeoutReached() {
  return inactivityTimeoutReached;
}

bool InfoScreen::isPageBackRequested() {
  return pageBackRequested;
}

void InfoScreen::resetPageBackRequest() {
  pageBackRequested = false;
}

void InfoScreen::resetScreen() {
  Serial.println("InfoScreen::resetScreen called - forcing redraw");
  screenInitialized = false;

  // IMPORTANT FIX: Pre-load time values from RTC before screen is redrawn
  if (tcpClient) {
    // Update connection status
    wifiConnected = tcpClient->isWiFiConnected();
    internetConnected = tcpClient->isInternetAvailable();
    mqttConnected = tcpClient->isMQTTConnected();
    wifiStrength = tcpClient->getWiFiSignalStrength();

    // Fetch current time from RTC (regardless of internet)
    currentDate = tcpClient->getCurrentDate();
    currentTime = tcpClient->getCurrentTime();
    currentDayOfWeek = tcpClient->getDayOfWeek();
    formatDate();
  }
}

void InfoScreen::printTouchDebugInfo() {
  if (touchPanel) {
    Serial.println("-----Touch Debug Info-----");
    Serial.print("Touch detected: ");
    Serial.println(touchPanel->isPressed() ? "YES" : "NO");
    Serial.print("New press: ");
    Serial.println(touchPanel->getHasNewPress() ? "YES" : "NO");
    Serial.print("X position: ");
    Serial.println(touchPanel->getTouchX());
    Serial.print("Y position: ");
    Serial.println(touchPanel->getTouchY());
    Serial.println("------------------------");
  }
}