#pragma once

#include "LGFX_Config.hpp"
#include "esp_timer.h"
#include <cstdint>
#include <algorithm>

class InitializationScreen {
private:
    LGFX* gfx;
    int progressValue;       // Current progress value (0-100)
    int previousProgress;    // Previous progress value for incremental updates
    bool screenInitialized;
    bool qnobTextDrawn;      // Flag to track whether QNOB text has been drawn

    // Network status indicators
    bool wifiConnected;
    int wifiStrength;        // 0-3 for signal strength
    bool mqttConnected;

    // Animation properties
    float animatedProgressAngle;  // Current animated angle (smoothly animated)
    float targetProgressAngle;    // Target angle based on progress
    float startProgressAngle;     // Starting angle for animation
    float lastRenderedProgressAngle;  // Last angle that has been drawn to the display
    int64_t animationStartTime;   // When current animation started
    int64_t lastAnimationUpdateTime;  // Last time animation was updated
    static constexpr int64_t ANIMATION_DURATION = 500;  // Animation duration in milliseconds
    static constexpr float PROGRESS_START_ANGLE = 120.0f;
    static constexpr float PROGRESS_ARC_SPAN = 300.0f;

    void updateAnimation();
    void fillArcWrapped(int centerX, int centerY, int outerRadius, int innerRadius,
                        float startAngle, float endAngle, uint16_t color);
    void drawBackgroundArc(int centerX, int centerY, int radius);
    void drawProgressArc(int centerX, int centerY, int radius);
    void drawQNOBText(int centerX, int centerY, int radius);

    // Helper to get current time in ms
    int64_t millis() const { return esp_timer_get_time() / 1000; }

    // Constrain value to range
    template<typename T>
    static T constrain(T value, T minVal, T maxVal) {
        return std::max(minVal, std::min(value, maxVal));
    }

    // Map value from one range to another
    static float mapValue(float value, float inMin, float inMax, float outMin, float outMax) {
        return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
    }

public:
    InitializationScreen(LGFX* graphics);

    void updateScreen();
    void setProgress(int progress);  // Set progress value (0-100)
    void setWiFiStatus(bool connected, int strength);
    void setMQTTStatus(bool isConnected);
    void reset();
};
