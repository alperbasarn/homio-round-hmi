#include "InitializationScreen.h"

InitializationScreen::InitializationScreen(Arduino_GFX* graphics)
  : gfx(graphics), progressValue(0), previousProgress(0),
    screenInitialized(false),
    animatedProgressAngle(0), targetProgressAngle(0),
    lastAnimationUpdateTime(0), qnobTextDrawn(false) {
}

void InitializationScreen::setProgress(int progress) {
  previousProgress = progressValue;
  progressValue = constrain(progress, 0, 100);
  Serial.println("PROGRESS UPDATED:" + String(progress));

  // Calculate new target angle based on progress
  float prevAngle = targetProgressAngle;
  targetProgressAngle = map(progressValue, 0, 100, 0, 360);

  // If this is a significant progress change, update animation start time
  if (abs(targetProgressAngle - prevAngle) > 1) {
    animationStartTime = millis();
    startProgressAngle = animatedProgressAngle;
  }

  updateScreen();
}


void InitializationScreen::setMQTTStatus(bool isConnected) {
  mqttConnected = isConnected;
}

void InitializationScreen::updateScreen() {
  int centerX = gfx->width() / 2;
  int centerY = gfx->height() / 2;
  int radius = min(centerX, centerY) - 2;  // Use almost full screen

  if (!screenInitialized) {
    // Clear screen with a dark background
    gfx->fillScreen(BLACK);

    // Draw a simple circular outline
    gfx->drawCircle(centerX, centerY, radius, gfx->color565(40, 40, 80));

    // Draw background arc (dim blue) - continuous around the edge
    drawBackgroundArc(centerX, centerY, radius);

    // Initialize to 0% progress
    animatedProgressAngle = 0;
    startProgressAngle = 0;
    targetProgressAngle = 0;
    animationStartTime = millis();

    screenInitialized = true;

    // Draw QNOB text once at the beginning
    drawQNOBText(centerX, centerY, radius);
  } else {
    // Update animation
    unsigned long currentMillis = millis();
    if (currentMillis - lastAnimationUpdateTime >= 16) {  // ~60fps update rate
      updateAnimation();
      lastAnimationUpdateTime = currentMillis;

      // Redraw the progress arc with the current animated angle
      drawProgressArc(centerX, centerY, radius);
    }
  }
}

void InitializationScreen::updateAnimation() {
  unsigned long currentMillis = millis();

  // Calculate animation progress (0.0 to 1.0)
  float progress = constrain((float)(currentMillis - animationStartTime) / ANIMATION_DURATION, 0.0, 1.0);

  // Use easing function for smoother animation (ease-out cubic)
  float easedProgress = 1.0 - pow(1.0 - progress, 3);

  // Interpolate between start and target angles
  animatedProgressAngle = startProgressAngle + (targetProgressAngle - startProgressAngle) * easedProgress;
}

void InitializationScreen::drawBackgroundArc(int centerX, int centerY, int radius) {
  // Thicker arc (30px)
  int arcThickness = 30;
  int arcRadius = radius - (arcThickness / 2);

  // Define the step size for smoother arc
  const float angleStep = 0.5;

  // Draw background arc (dim blue) using triangles
  for (float angle = 0; angle < 360; angle += angleStep) {
    float nextAngle = angle + angleStep;

    // Convert angles to radians
    float rad1 = angle * PI / 180.0;
    float rad2 = nextAngle * PI / 180.0;

    // Calculate points for arc segment
    int x1Inner = centerX + (arcRadius - arcThickness / 2) * cos(rad1);
    int y1Inner = centerY + (arcRadius - arcThickness / 2) * sin(rad1);
    int x2Inner = centerX + (arcRadius - arcThickness / 2) * cos(rad2);
    int y2Inner = centerY + (arcRadius - arcThickness / 2) * sin(rad2);

    int x1Outer = centerX + (arcRadius + arcThickness / 2) * cos(rad1);
    int y1Outer = centerY + (arcRadius + arcThickness / 2) * sin(rad1);
    int x2Outer = centerX + (arcRadius + arcThickness / 2) * cos(rad2);
    int y2Outer = centerY + (arcRadius + arcThickness / 2) * sin(rad2);

    // Draw arc segment (filled quadrilateral using two triangles)
    gfx->fillTriangle(x1Inner, y1Inner, x1Outer, y1Outer, x2Outer, y2Outer, gfx->color565(10, 10, 40));
    gfx->fillTriangle(x1Inner, y1Inner, x2Inner, y2Inner, x2Outer, y2Outer, gfx->color565(10, 10, 40));
  }
}

void InitializationScreen::drawProgressArc(int centerX, int centerY, int radius) {
  // Thicker arc (30px)
  int arcThickness = 30;
  int arcRadius = radius - (arcThickness / 2);

  // Use a smaller angle step for smoother arcs
  const float angleStep = 0.5;

  // Red color for progress arc
  uint16_t progressColor = RED;

  // Draw progress arc using triangles
  for (float angle = 0; angle < animatedProgressAngle; angle += angleStep) {
    float nextAngle = min(angle + angleStep, animatedProgressAngle);

    // Convert angles to radians
    float rad1 = angle * PI / 180.0;
    float rad2 = nextAngle * PI / 180.0;

    // Calculate points for arc segment
    int x1Inner = centerX + (arcRadius - arcThickness / 2) * cos(rad1);
    int y1Inner = centerY + (arcRadius - arcThickness / 2) * sin(rad1);
    int x2Inner = centerX + (arcRadius - arcThickness / 2) * cos(rad2);
    int y2Inner = centerY + (arcRadius - arcThickness / 2) * sin(rad2);

    int x1Outer = centerX + (arcRadius + arcThickness / 2) * cos(rad1);
    int y1Outer = centerY + (arcRadius + arcThickness / 2) * sin(rad1);
    int x2Outer = centerX + (arcRadius + arcThickness / 2) * cos(rad2);
    int y2Outer = centerY + (arcRadius + arcThickness / 2) * sin(rad2);

    // Draw arc segment (filled quadrilateral using two triangles)
    gfx->fillTriangle(x1Inner, y1Inner, x1Outer, y1Outer, x2Outer, y2Outer, progressColor);
    gfx->fillTriangle(x1Inner, y1Inner, x2Inner, y2Inner, x2Outer, y2Outer, progressColor);
  }
}

void InitializationScreen::drawQNOBText(int centerX, int centerY, int radius) {
  // Clear the center area for QNOB text
  gfx->fillCircle(centerX, centerY, radius * 0.6, BLACK);

  // Set text size for QNOB
  gfx->setTextSize(5);

  // Calculate text dimensions for proper centering
  int16_t qx1, qy1, qnobx1, qnoby1;
  uint16_t qw, qh, qnobw, qnobh;

  // Get bounds of "Q" and "QNOB" to calculate total width
  gfx->getTextBounds("Q", 0, 0, &qx1, &qy1, &qw, &qh);
  gfx->getTextBounds("QNOB", 0, 0, &qnobx1, &qnoby1, &qnobw, &qnobh);

  // Calculate starting position to center "QNOB"
  int startX = centerX - (qnobw / 2);
  int textY = centerY - (qh / 2);  // Vertically center

  // First draw the "Q" in red
  gfx->setTextColor(RED);
  gfx->setCursor(startX, textY);
  gfx->print("Q");

  // Then draw "NOB" in white right after Q
  gfx->setTextColor(WHITE);
  gfx->setCursor(startX + qw, textY);
  gfx->print("NOB");

  qnobTextDrawn = true;
}

void InitializationScreen::setWiFiStatus(bool connected, int strength) {
  wifiConnected = connected;
  wifiStrength = constrain(strength, 0, 3);
}

void InitializationScreen::reset() {
  progressValue = 0;
  previousProgress = 0;
  wifiConnected = false;
  wifiStrength = 0;
  mqttConnected = false;
  screenInitialized = false;
  qnobTextDrawn = false;
  animatedProgressAngle = 0;
  targetProgressAngle = 0;
  startProgressAngle = 0;
  animationStartTime = millis();
}