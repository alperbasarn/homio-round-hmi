#include "InitializationScreen.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "InitScreen";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

InitializationScreen::InitializationScreen(LGFX* graphics)
    : gfx(graphics), progressValue(0), previousProgress(0),
      screenInitialized(false), qnobTextDrawn(false),
      wifiConnected(false), wifiStrength(0), mqttConnected(false),
      animatedProgressAngle(0), targetProgressAngle(0),
      startProgressAngle(0), lastRenderedProgressAngle(0),
      animationStartTime(0),
      lastAnimationUpdateTime(0) {
}

void InitializationScreen::setProgress(int progress) {
    previousProgress = progressValue;
    progressValue = constrain(progress, 0, 100);
    ESP_LOGI(TAG, "Progress updated: %d", progress);

    // Calculate new target angle based on progress
    float prevAngle = targetProgressAngle;
    targetProgressAngle = mapValue(static_cast<float>(progressValue), 0, 100, 0.0f, PROGRESS_ARC_SPAN);

    // If this is a significant progress change, update animation start time
    if (std::abs(targetProgressAngle - prevAngle) > 1) {
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
    int radius = std::min(centerX, centerY) - std::max(2, gfx->width() / 32);

    if (!screenInitialized) {
        gfx->fillScreen(TFT_BLACK);
        gfx->drawCircle(centerX, centerY, radius, gfx->color565(24, 30, 40));

        drawBackgroundArc(centerX, centerY, radius);

        animatedProgressAngle = 0;
        startProgressAngle = 0;
        targetProgressAngle = 0;
        lastRenderedProgressAngle = 0;
        animationStartTime = millis();

        screenInitialized = true;
        drawQNOBText(centerX, centerY, radius);
    }

    int64_t currentMillis = millis();
    if (currentMillis - lastAnimationUpdateTime >= 16) {  // ~60fps update rate
        updateAnimation();
        lastAnimationUpdateTime = currentMillis;
        drawProgressArc(centerX, centerY, radius);
    }
}

void InitializationScreen::updateAnimation() {
    int64_t currentMillis = millis();

    // Calculate animation progress (0.0 to 1.0)
    float progress = constrain(static_cast<float>(currentMillis - animationStartTime) / ANIMATION_DURATION, 0.0f, 1.0f);

    // Use easing function for smoother animation (ease-out cubic)
    float easedProgress = 1.0f - std::pow(1.0f - progress, 3);

    // Interpolate between start and target angles
    animatedProgressAngle = startProgressAngle + (targetProgressAngle - startProgressAngle) * easedProgress;
}

void InitializationScreen::fillArcWrapped(int centerX, int centerY, int outerRadius, int innerRadius,
                                          float startAngle, float endAngle, uint16_t color) {
    while (startAngle < 0.0f) {
        startAngle += 360.0f;
    }
    while (endAngle < 0.0f) {
        endAngle += 360.0f;
    }
    while (startAngle >= 360.0f) {
        startAngle -= 360.0f;
    }
    while (endAngle >= 360.0f) {
        endAngle -= 360.0f;
    }

    if (endAngle >= startAngle) {
        gfx->fillArc(centerX, centerY, outerRadius, innerRadius, startAngle, endAngle, color);
    } else {
        gfx->fillArc(centerX, centerY, outerRadius, innerRadius, startAngle, 360.0f, color);
        gfx->fillArc(centerX, centerY, outerRadius, innerRadius, 0.0f, endAngle, color);
    }
}

void InitializationScreen::drawBackgroundArc(int centerX, int centerY, int radius) {
    int arcThickness = std::max(12, (14 * gfx->width()) / 240);
    int outerRadius = radius;
    int innerRadius = radius - arcThickness;
    int midRadius = (outerRadius + innerRadius) / 2;
    int capRadius = std::max(2, arcThickness / 2);

    uint16_t bgColor = gfx->color565(18, 26, 42);
    fillArcWrapped(centerX, centerY, outerRadius, innerRadius,
                   PROGRESS_START_ANGLE, PROGRESS_START_ANGLE + PROGRESS_ARC_SPAN, bgColor);

    float startRad = PROGRESS_START_ANGLE * M_PI / 180.0f;
    float endRad = (PROGRESS_START_ANGLE + PROGRESS_ARC_SPAN) * M_PI / 180.0f;
    int startX = centerX + static_cast<int>(std::round(midRadius * std::cos(startRad)));
    int startY = centerY + static_cast<int>(std::round(midRadius * std::sin(startRad)));
    int endX = centerX + static_cast<int>(std::round(midRadius * std::cos(endRad)));
    int endY = centerY + static_cast<int>(std::round(midRadius * std::sin(endRad)));
    gfx->fillCircle(startX, startY, capRadius, bgColor);
    gfx->fillCircle(endX, endY, capRadius, bgColor);
}

void InitializationScreen::drawProgressArc(int centerX, int centerY, int radius) {
    if (animatedProgressAngle <= lastRenderedProgressAngle + 0.05f) {
        return;
    }

    int arcThickness = std::max(12, (14 * gfx->width()) / 240);
    int outerRadius = radius;
    int innerRadius = radius - arcThickness;
    int midRadius = (outerRadius + innerRadius) / 2;
    int capRadius = std::max(2, arcThickness / 2);

    uint16_t progressColor = gfx->color565(255, 72, 88);
    float start = PROGRESS_START_ANGLE + lastRenderedProgressAngle;
    float end = PROGRESS_START_ANGLE + animatedProgressAngle;
    fillArcWrapped(centerX, centerY, outerRadius, innerRadius, start, end, progressColor);

    if (animatedProgressAngle > 0.2f) {
        float startRad = PROGRESS_START_ANGLE * M_PI / 180.0f;
        float endRad = (PROGRESS_START_ANGLE + animatedProgressAngle) * M_PI / 180.0f;
        int startX = centerX + static_cast<int>(std::round(midRadius * std::cos(startRad)));
        int startY = centerY + static_cast<int>(std::round(midRadius * std::sin(startRad)));
        int endX = centerX + static_cast<int>(std::round(midRadius * std::cos(endRad)));
        int endY = centerY + static_cast<int>(std::round(midRadius * std::sin(endRad)));
        gfx->fillCircle(startX, startY, capRadius, progressColor);
        gfx->fillCircle(endX, endY, capRadius, progressColor);
    }

    lastRenderedProgressAngle = animatedProgressAngle;
}

void InitializationScreen::drawQNOBText(int centerX, int centerY, int radius) {
    // Clear the center area for QNOB text
    gfx->fillCircle(centerX, centerY, radius * 0.6f, TFT_BLACK);

    // Keep the logo readable without overwhelming the center disc.
    int textScale = std::max(2, gfx->width() / 120);  // ~2 for 240px, ~3 for 466px
    gfx->setTextSize(textScale);

    // Calculate text dimensions for proper centering
    // For LovyanGFX, we use textWidth instead of getTextBounds
    int qWidth = gfx->textWidth("Q");
    int qnobWidth = gfx->textWidth("QNOB");
    int textHeight = gfx->fontHeight();

    // Calculate starting position to center "QNOB"
    int startX = centerX - (qnobWidth / 2);
    int textY = centerY - (textHeight / 2);

    // First draw the "Q" in red
    gfx->setTextColor(TFT_RED);
    gfx->setCursor(startX, textY);
    gfx->print("Q");

    // Then draw "NOB" in white right after Q
    gfx->setTextColor(TFT_WHITE);
    gfx->setCursor(startX + qWidth, textY);
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
    lastRenderedProgressAngle = 0;
    animationStartTime = millis();
}
