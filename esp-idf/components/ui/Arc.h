#pragma once

#include "LvglDisplay.h"
#include <cstdint>

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
    int centerX, centerY;
    int radius;
    int arcWidth;
    int maxArcLength;
    int startAngle;
    int currentPercentage;
    int previousPercentage;
    int targetPercentage;
    int animationSpeed;
    uint32_t arcColor;       // RGB888
    uint32_t backgroundColor;// RGB888

    bool segmentVisible;
    int currentSegmentPercentage;
    int prevSegmentPercentage;
    uint32_t segmentColor;   // RGB888

    bool segmentAnimationActive;
    AnimationStep currentAnimStep;
    int savedSetpoint;
    int animationCounter;
    int animationStartPoint;
    int animationEndPoint;

    int64_t arcAnimationStartTime;
    int64_t arcAnimationDuration;
    int arcAnimationStartPoint;
    int64_t stepStartTime;

    int   constrain(int value, int min, int max);
    float constrainf(float value, float min, float max);

    void drawThickArc(int cx, int cy, int outerR, int innerR,
                      float startAngle, float endAngle, uint32_t color);

public:
    Arc();

    void initialize(int centerX, int centerY, int radius, int arcWidth,
                    int maxArcLength, int startAngle = 120);
    void update();
    void updateAnimationStep();
    void draw();

    void drawSegment(int startPercent, int endPercent);
    void drawFullSegment();
    void clearSegment(int startPercent, int endPercent);
    void clearFullSegment();
    void clearArc();

    void setColor(uint32_t rgb888);
    void setPercentage(int percentage);
    void reset();

    void startSegmentAnimation();
    void stopSegmentAnimation();
    bool isSegmentAnimationActive() const { return segmentAnimationActive; }
    int  getPercentage()            const { return currentPercentage; }
};
