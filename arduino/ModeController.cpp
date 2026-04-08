#include "ModeController.h"

ModeController::ModeController(Arduino_GFX* graphics, TouchPanel* touch, WiFiTCPClient* client)
  : gfx(graphics), touchPanel(touch), tcpClient(client), currentMode(2), lastMode(-1), active(false) {}

void ModeController::updateScreen() {
  if (!active) {
    return;  // Skip updates if the controller is inactive
  }

  if (currentMode != lastMode) {
    lastMode = currentMode;  // Update the last drawn mode
    drawModeLogo();
  }
}

void ModeController::nextMode() {
  currentMode = (currentMode + 1) % modeCount;
  drawModeLogo();
}

void ModeController::previousMode() {
  currentMode = (currentMode - 1 + modeCount) % modeCount;
  drawModeLogo();
}

void ModeController::setActive(bool isActive) {
  active = isActive;
  drawModeLogo();
}

void ModeController::setTCPClient(WiFiTCPClient* client) {
  tcpClient = client;
}

void ModeController::drawModeLogo() {
  gfx->fillScreen(BLACK);  // Clear the screen
  switch (currentMode) {
    case 0: drawBulb(); break;         // Light mode
    case 1: drawThermometer(); break;  // Temperature mode
    case 2: drawSpeaker(); break;      // Sound mode
  }
}

int ModeController::getCurrentMode() const {
  return currentMode;
}

bool ModeController::isSoundModeActive() const {
  return currentMode == 2;  // Return true if the current mode is Sound
}

bool ModeController::isLightModeActive() const {
  return currentMode == 0;  // Return true if the current mode is Light
}

// Simplified version of drawThermometer for a more minimal logo
void ModeController::drawThermometer() {
  int centerX = gfx->width() / 2;
  int centerY = gfx->height() / 2;
  
  // Draw a simple thermometer
  gfx->fillCircle(centerX, centerY + 30, 15, gfx->color565(255, 0, 0));   // Red bulb
  gfx->fillRect(centerX - 5, centerY - 40, 10, 70, gfx->color565(255, 0, 0));  // Red stem
  
  // Draw outline
  gfx->drawCircle(centerX, centerY + 30, 17, WHITE);
  gfx->drawRect(centerX - 7, centerY - 40, 14, 70, WHITE);
  
  // Draw temperature ticks
  for (int y = centerY - 30; y <= centerY + 10; y += 10) {
    gfx->drawLine(centerX - 12, y, centerX - 7, y, WHITE);
  }
  
  // Draw title at bottom
  gfx->setTextSize(2);
  gfx->setTextColor(WHITE);
  gfx->setCursor(centerX - 70, centerY + 60);
  gfx->print("TEMPERATURE");
}

// Simplified version of drawBulb for a more minimal logo
void ModeController::drawBulb() {
  int centerX = gfx->width() / 2;
  int centerY = gfx->height() / 2;
  
  // Draw a simple light bulb as a circle with rays
  gfx->fillCircle(centerX, centerY, 25, gfx->color565(255, 255, 0));  // Yellow bulb
  
  // Draw bulb base
  gfx->fillRect(centerX - 7, centerY + 25, 14, 15, gfx->color565(180, 180, 180));
  
  // Add short rays around the bulb
  for (int i = 0; i < 360; i += 45) {  // Rays at every 45 degrees
    float rad = i * PI / 180;
    int x1 = centerX + 30 * cos(rad);
    int y1 = centerY + 30 * sin(rad);
    int x2 = centerX + 40 * cos(rad);
    int y2 = centerY + 40 * sin(rad);
    gfx->drawLine(x1, y1, x2, y2, gfx->color565(255, 255, 0));  // Yellow rays
  }
  
  // Draw title at bottom
  gfx->setTextSize(2);
  gfx->setTextColor(WHITE);
  gfx->setCursor(centerX - 30, centerY + 60);
  gfx->print("LIGHT");
}

// Simplified version of drawSpeaker for a more minimal logo
void ModeController::drawSpeaker() {
  int centerX = gfx->width() / 2;
  int centerY = gfx->height() / 2;
  
  // Speaker body
  gfx->fillRect(centerX - 25, centerY - 20, 20, 40, gfx->color565(192, 192, 192));
  
  // Speaker cone
  gfx->fillTriangle(
    centerX - 5, centerY - 30,
    centerX - 5, centerY + 30,
    centerX + 20, centerY,
    gfx->color565(160, 160, 160)
  );
  
  // Speaker outline
  gfx->drawRect(centerX - 25, centerY - 20, 20, 40, WHITE);
  gfx->drawTriangle(
    centerX - 5, centerY - 30,
    centerX - 5, centerY + 30,
    centerX + 20, centerY,
    WHITE
  );
  
  // Sound waves
  gfx->drawArc(centerX + 25, centerY, 10, 5, 270, 90, WHITE);
  gfx->drawArc(centerX + 35, centerY, 15, 5, 270, 90, WHITE);
  
  // Draw title at bottom
  gfx->setTextSize(2);
  gfx->setTextColor(WHITE);
  gfx->setCursor(centerX - 40, centerY + 60);
  gfx->print("SOUND");
}