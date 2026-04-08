#include "LightController.h"

// Constructor
LightController::LightController(Arduino_GFX* graphics, TouchPanel* touch, MQTTHandler* mqttHandler)
  : gfx(graphics), touchPanel(touch), mqttHandler(mqttHandler), setpoint(50), lastSetpoint(-1),
    lastSentSetpoint(-1), lastSentColor(0), initialized(false), pageBackRequested(false), waterLevel(50), colorHue(0.5f),
    lastWaterLevel(-1), lastColorHue(-1), lastColorChangeTime(0), lastWaterLevelSentTime(0), lastSetPointDrawnTime(0),
    lastScreenUpdateTime(0), firstTouchX(-1), firstTouchY(-1), currentMode(STATIC), modeChangeRequested(false) {
    
    // Initialize state variables
    colorChange = false;
    waterLevelChange = false;
    lastSetpointChangeTime = 0;
    lastSetpointQueryTime = 0;
}

// Helper function to convert HSV to RGB565
uint16_t LightController::hsvToRgb565(float hue, float saturation, float value) {
  float r, g, b;
  int i = int(hue * 6);
  float f = hue * 6 - i;
  float p = value * (1 - saturation);
  float q = value * (1 - f * saturation);
  float t = value * (1 - (1 - f) * saturation);

  switch (i % 6) {
    case 0: r = value, g = t, b = p; break;
    case 1: r = q, g = value, b = p; break;
    case 2: r = p, g = value, b = t; break;
    case 3: r = p, g = q, b = value; break;
    case 4: r = t, g = p, b = value; break;
    case 5: r = value, g = p, b = q; break;
  }

  uint8_t red = r * 255;
  uint8_t green = g * 255;
  uint8_t blue = b * 255;

  return gfx->color565(red, green, blue);
}

void LightController::drawSetPoint() {
  int height = gfx->height();
  int width = gfx->width();
  int waterLevelHeight = (waterLevel * height) / 100;

  // Determine the color based on the hue
  uint16_t color = hsvToRgb565(colorHue, 1.0f, 1.0f);

  // Clear only the water level region
  int lastWaterLevelHeight = (lastWaterLevel * height) / 100;
  gfx->fillRect(0, height - max(waterLevelHeight, lastWaterLevelHeight), width, abs(waterLevelHeight - lastWaterLevelHeight), BLACK);
  // Fill the water level region with the current color
  gfx->fillRect(0, height - waterLevelHeight, width, waterLevelHeight, color);

  lastWaterLevel = waterLevel;
  lastColorHue = colorHue;
  lastSetpoint = setpoint;  // Update the last drawn setpoint
}

void LightController::incrementSetpoint() {
  if (waterLevel < 100) {
    waterLevel ++;
    waterLevelChange = true;
    lastSetpointChangeTime = millis();
  }
}

void LightController::decrementSetpoint() {
  if (waterLevel > 0) {
    waterLevel --;
    waterLevelChange = true;
    lastSetpointChangeTime = millis();
  }
}

void LightController::setSetpoint(int setpoint) {
  if (setpoint >= 0 && setpoint <= 100) {
    waterLevel = setpoint;
    waterLevelChange = true;
    lastSetpointChangeTime = millis();
  }
}

void LightController::updateScreen() {
  if (!initialized) {
    drawStaticElements();
    initialized = true;
    Serial.println("Static elements drawn for LightController.");
    requestCurrentBrightnessFromServer();
  } else {
    unsigned long currentMillis = millis();

    checkTouchInput();
    // Redraw the water level only if it or the color has changed
    if ((waterLevelChange || colorChange) && (currentMillis - lastSetPointDrawnTime >= 10)) {
      drawSetPoint();
      sendSetpointToServer();
      lastSetPointDrawnTime = currentMillis;
      colorChange = false;
      waterLevelChange = false;
    }
    // Draw mode text and buttons
    drawModeText();
  }
  
  // Ask the knob for updated setpoint information
  /*
  if (knob && millis() > lastSetpointQueryTime + 10) {
    knob->askSetpoint();
    lastSetpointQueryTime = millis();
  }
  */
}

void LightController::checkTouchInput() {
  if (touchPanel->isPressed()) {
    int touchX = touchPanel->getTouchX();
    int touchY = touchPanel->getTouchY();
    int screenWidth = gfx->width();
    int screenHeight = gfx->height();

    if (touchPanel->getHasNewPress()) {
      firstTouchX = touchX;
      firstTouchY = touchY;
    }

    // Calculate swipe delta
    int deltaX = touchX - firstTouchX;
    int deltaY = touchY - firstTouchY;

    // Only update if swipe length is more than 10 pixels
    if (abs(deltaX) > 10 || abs(deltaY) > 10) {
      // Update the hue based on X-axis swipe
      if (abs(deltaX) > 10) {
        colorHue = (float)touchX / screenWidth;
        colorChange = true;
      }

      // Update the water level based on Y-axis swipe
      if (abs(deltaY) > 10) {
        waterLevel = (1.0f - ((float)touchY / screenHeight)) * 100;
        // Make sure waterLevel stays in valid range
        waterLevel = constrain(waterLevel, 0, 100);
        waterLevelChange = true;
      }
    }
  } else {
    firstTouchX = -1;
    firstTouchY = -1;

    if (touchPanel->getHasNewHoldRelease()) {
      if (!pageBackRequested) {
        if (!colorChange && !waterLevelChange) {
          pageBackRequested = true;
          Serial.println("Back requested from Light Controller.");
        }
      }
    }
  }
}

void LightController::sendSetpointToServer() {
  // Make sure MQTT is configured and the handler exists
  if (!mqttHandler || !mqttHandler->hasLightMQTTConfigured()) {
    Serial.println("Light MQTT not configured, skipping setpoint send");
    return;
  }
  
  // Use MQTT to send LED ring commands
  uint16_t color = hsvToRgb565(colorHue, 1.0f, 1.0f);
  uint8_t r = (color >> 11) & 0x1F;
  uint8_t g = (color >> 5) & 0x3F;
  uint8_t b = color & 0x1F;

  r = (r * 255 / 31) * (waterLevel / 100.0f);
  g = (g * 255 / 63) * (waterLevel / 100.0f);
  b = (b * 255 / 31) * (waterLevel / 100.0f);

  String colorMessage = String(r) + "," + String(g) + "," + String(b);
  String brightnessMessage = String((waterLevel * 255) / 100);
  String modeMessage;
  
  switch (currentMode) {
    case STATIC: modeMessage = "static"; break;
    case GLOW: modeMessage = "glow"; break;
    case RAINBOW: modeMessage = "rainbow"; break;
  }

  // Send messages only if handler exists
  if (mqttHandler) {
    mqttHandler->sendMQTTMessage("ledRing/colorControl", colorMessage);
    mqttHandler->sendMQTTMessage("ledRing/brightnessControl", brightnessMessage);
    mqttHandler->sendMQTTMessage("ledRing/modeControl", modeMessage);
  }
}

void LightController::drawStaticElements() {
  if (!gfx) {
    Serial.println("⚠️ Error: gfx is NULL, cannot draw UI.");
    return;
  }
  gfx->fillScreen(BLACK);
  drawSetPoint();
}

void LightController::requestCurrentBrightnessFromServer() {
  // Check if MQTT is configured and handler exists
  if (mqttHandler && mqttHandler->hasLightMQTTConfigured()) {
    // Use MQTT to request current brightness
    mqttHandler->sendMQTTMessage("esp32/light/get_brightness", "");
    Serial.println("Sent brightness request via MQTT.");
  } else {
    Serial.println("Light MQTT not configured, using default brightness");
  }
  
  // Default to current value until we receive a response via MQTT callback
  /*
  String setpointString = "setpoint=" + String(waterLevel);
  if (knob) {
    knob->sendCommand(setpointString);
  }
  */
}

bool LightController::isPageBackRequested() const {
  return pageBackRequested;
}

void LightController::resetPageBackRequest() {
  pageBackRequested = false;
  initialized = false;
}

void LightController::drawModeText() {
  int screenWidth = gfx->width();
  int screenHeight = gfx->height();
  gfx->setTextSize(2);
  gfx->setTextColor(WHITE);

  // Draw mode text
  String modeText;
  switch (currentMode) {
    case STATIC: modeText = "Static"; break;
    case GLOW: modeText = "Glow"; break;
    case RAINBOW: modeText = "Rainbow"; break;
  }
  int textSize = 2;   // Adjust this based on your text size
  int charWidth = 6;  // Approximate width of a single character in default font
  int textWidth = modeText.length() * charWidth * textSize;
  gfx->setCursor((screenWidth - textWidth) / 2, screenHeight - 30);
  gfx->print(modeText);

  // Draw back and forward buttons
  gfx->fillTriangle(10, screenHeight - 20, 30, screenHeight - 40, 30, screenHeight - 10, WHITE);                                            // Back button
  gfx->fillTriangle(screenWidth - 10, screenHeight - 20, screenWidth - 30, screenHeight - 40, screenWidth - 30, screenHeight - 10, WHITE);  // Forward button
}

// Update MQTT handler
void LightController::setMQTTHandler(MQTTHandler* handler) {
  mqttHandler = handler;
}

void LightController::handleModeChange() {
  if (modeChangeRequested) {
    currentMode = static_cast<LedMode>((currentMode + 1) % 3);  // Cycle through modes
    modeChangeRequested = false;
    drawModeText();
    
    // Send the new mode to the server
    sendSetpointToServer();
  }
}
