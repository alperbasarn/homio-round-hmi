#pragma once

#include "LGFX_Config.hpp"
#include <cstdint>

// Animation step enumeration
enum class AnimationStep {
    NONE,
    SHRINK_ARC_TO_ZERO,
    SHRINK_SEGMENT_TO_START,
    GROW_SEGMENT_START_TO_END,
    SHRINK_SEGMENT_START_TO_END,
    GROW_SEGMENT_END_TO_START,
    SHRINK_SEGMENT_END_TO_START,
    GROW_ARC_TO_SETPOINT
};

class Arc {
private:
    LGFX* gfx;
    int centerX, centerY;
    int radius;
    int arcWidth;
    int maxArcLength;
    int startAngle;
    int currentPercentage;
    int previousPercentage;
    int targetPercentage;
    int animationSpeed;
    uint16_t arcColor;
    uint16_t backgroundColor;

    // Segment properties
    bool segmentVisible;
    int currentSegmentPercentage;
    int prevSegmentPercentage;
    uint16_t segmentColor;

    // Animation properties
    bool segmentAnimationActive;
    AnimationStep currentAnimStep;
    int savedSetpoint;
    int animationCounter;
    int animationStartPoint;
    int animationEndPoint;

    // Time-based animation
    int64_t arcAnimationStartTime;
    int64_t arcAnimationDuration;
    int arcAnimationStartPoint;
    int64_t stepStartTime;

    // Helper functions
    int constrain(int value, int min, int max);
    float constrainf(float value, float min, float max);

    // Custom arc drawing for QSPI panel compatibility
    void drawThickArc(int cx, int cy, int outerR, int innerR,
                      float startAngle, float endAngle, uint16_t color);

public:
    Arc(LGFX* graphics);

    void initialize(int centerX, int centerY, int radius, int arcWidth,
                   int maxArcLength, int startAngle = 120);
    void update();
    void updateAnimationStep();
    void draw();

    // Segment operations
    void drawSegment(int startPercent, int endPercent);
    void drawFullSegment();
    void clearSegment(int startPercent, int endPercent);
    void clearFullSegment();
    void clearArc();

    // Properties
    void setColor(uint16_t color);
    void setPercentage(int percentage);
    void reset();

    // Animation control
    void startSegmentAnimation();
    void stopSegmentAnimation();
    bool isSegmentAnimationActive() const { return segmentAnimationActive; }
    int getPercentage() const { return currentPercentage; }
};
