#include "Arc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Arc::Arc()
    : centerX(0), centerY(0), radius(0), arcWidth(0),
      maxArcLength(0), startAngle(0), currentPercentage(0), previousPercentage(0),
      targetPercentage(0), animationSpeed(5),
      arcColor(0xFFFFFF), backgroundColor(0x000000),
      segmentVisible(true), currentSegmentPercentage(100),
      prevSegmentPercentage(100), segmentColor(0x404040),
      segmentAnimationActive(false), currentAnimStep(AnimationStep::NONE),
      savedSetpoint(0), animationCounter(0), animationStartPoint(0),
      animationEndPoint(100), arcAnimationStartTime(0), arcAnimationDuration(300),
      arcAnimationStartPoint(0), stepStartTime(0) {}

int Arc::constrain(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

float Arc::constrainf(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

void Arc::drawThickArc(int cx, int cy, int outerR, int innerR,
                        float start, float end, uint32_t color) {
    float startRad = start * static_cast<float>(M_PI) / 180.0f;
    float endRad   = end   * static_cast<float>(M_PI) / 180.0f;
    if (endRad < startRad) endRad += 2.0f * static_cast<float>(M_PI);
    const float step = 0.3f * static_cast<float>(M_PI) / 180.0f;
    for (float a = startRad; a <= endRad; a += step) {
        float c = std::cos(a), s = std::sin(a);
        LvglDisplay::drawLine(cx + static_cast<int>(innerR * c), cy - static_cast<int>(innerR * s),
                              cx + static_cast<int>(outerR * c), cy - static_cast<int>(outerR * s),
                              color);
    }
    float c = std::cos(endRad), s = std::sin(endRad);
    LvglDisplay::drawLine(cx + static_cast<int>(innerR * c), cy - static_cast<int>(innerR * s),
                          cx + static_cast<int>(outerR * c), cy - static_cast<int>(outerR * s),
                          color);
}

void Arc::initialize(int cx, int cy, int rad, int width, int length, int start) {
    centerX = cx; centerY = cy; radius = rad; arcWidth = width;
    maxArcLength = length; startAngle = start;
    currentPercentage = previousPercentage = targetPercentage = 0;
    segmentVisible = true;
    currentSegmentPercentage = prevSegmentPercentage = 0;
    segmentAnimationActive = false;
    currentAnimStep = AnimationStep::NONE;
    savedSetpoint = animationCounter = animationStartPoint = 0;
    animationEndPoint = 100;
    arcAnimationStartTime = 0;
    drawFullSegment();
}

void Arc::update() {
    if (segmentAnimationActive) { updateAnimationStep(); return; }
    int64_t now = esp_timer_get_time() / 1000;
    if (currentPercentage != targetPercentage && arcAnimationStartTime == 0) {
        previousPercentage = currentPercentage;
        arcAnimationStartTime = now;
        arcAnimationStartPoint = currentPercentage;
    }
    if (arcAnimationStartTime > 0) {
        float p = constrainf(static_cast<float>(now - arcAnimationStartTime) /
                             static_cast<float>(arcAnimationDuration), 0.0f, 1.0f);
        currentPercentage = arcAnimationStartPoint +
            static_cast<int>((targetPercentage - arcAnimationStartPoint) * p);
        draw();
        if (p >= 1.0f) { currentPercentage = targetPercentage; arcAnimationStartTime = 0; }
    }
}

void Arc::draw() {
    if (currentPercentage == previousPercentage) return;
    float cur = constrainf(static_cast<float>(maxArcLength * currentPercentage) / 100.0f,
                           0.0f, static_cast<float>(maxArcLength) - 0.5f);
    float prv = constrainf(static_cast<float>(maxArcLength * previousPercentage) / 100.0f,
                           0.0f, static_cast<float>(maxArcLength) - 0.5f);
    if (currentPercentage > previousPercentage) {
        if (startAngle + prv != startAngle + cur)
            drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                         startAngle + prv, startAngle + cur, arcColor);
    } else {
        if (startAngle + cur != startAngle + prv)
            drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                         startAngle + cur, startAngle + prv, segmentColor);
    }
    previousPercentage = currentPercentage;
}

void Arc::drawSegment(int sp, int ep) {
    sp = constrain(sp, 0, 100); ep = constrain(ep, 0, 100);
    float sd = startAngle + constrainf(static_cast<float>(maxArcLength * sp) / 100.0f, 0, static_cast<float>(maxArcLength) - 0.5f);
    float ed = startAngle + constrainf(static_cast<float>(maxArcLength * ep) / 100.0f, 0, static_cast<float>(maxArcLength) - 0.5f);
    if (sd < ed) drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2, sd, ed, segmentColor);
}
void Arc::drawFullSegment() { drawSegment(0, 100); }

void Arc::clearSegment(int sp, int ep) {
    sp = constrain(sp, 0, 100); ep = constrain(ep, 0, 100);
    float sd = startAngle + constrainf(static_cast<float>(maxArcLength * sp) / 100.0f, 0, static_cast<float>(maxArcLength) - 0.5f);
    float ed = startAngle + constrainf(static_cast<float>(maxArcLength * ep) / 100.0f, 0, static_cast<float>(maxArcLength) - 0.5f);
    if (sd < ed) drawThickArc(centerX, centerY, radius + arcWidth/2 + 1, radius - arcWidth/2 - 1,
                               sd, ed, backgroundColor);
}
void Arc::clearFullSegment() {
    drawThickArc(centerX, centerY, radius + arcWidth/2 + 2, radius - arcWidth/2 - 2,
                 startAngle - 2, startAngle + maxArcLength + 2, backgroundColor);
}
void Arc::clearArc() {
    drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                 startAngle, startAngle + maxArcLength, segmentColor);
}

void Arc::setColor(uint32_t rgb888) {
    arcColor = rgb888;
    // Dimmed version for the inactive segment
    segmentColor = ((arcColor >> 1) & 0x7F7F7F);
    if (currentPercentage > 0) {
        float len = constrainf(static_cast<float>(maxArcLength * currentPercentage) / 100.0f,
                               0, static_cast<float>(maxArcLength) - 0.5f);
        drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                     startAngle, startAngle + len, arcColor);
    }
}

void Arc::setPercentage(int pct) {
    pct = constrain(pct, 0, 100);
    if (!segmentAnimationActive) {
        if (targetPercentage != pct) { targetPercentage = pct; arcAnimationStartTime = 0; }
    } else {
        savedSetpoint = pct;
    }
}

void Arc::reset() {
    currentPercentage = previousPercentage = targetPercentage = 0;
    arcAnimationStartTime = 0;
    clearFullSegment();
    segmentAnimationActive = false;
    currentSegmentPercentage = prevSegmentPercentage = 0;
    int64_t t0 = esp_timer_get_time() / 1000;
    int last = 0;
    while (true) {
        float p = constrainf(static_cast<float>(esp_timer_get_time() / 1000 - t0) / 500.0f, 0, 1);
        int seg = static_cast<int>(p * 100);
        if (seg > last) { drawSegment(last, seg); last = seg; }
        if (p >= 1.0f) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    drawFullSegment();
}

void Arc::startSegmentAnimation() {
    if (!segmentAnimationActive) {
        segmentAnimationActive = true;
        savedSetpoint = targetPercentage;
        clearArc();
        currentAnimStep = AnimationStep::SHRINK_ARC_TO_ZERO;
        currentPercentage = previousPercentage = 0;
        currentSegmentPercentage = prevSegmentPercentage = 100;
        animationCounter = animationStartPoint = 0;
        animationEndPoint = 100;
        stepStartTime = esp_timer_get_time() / 1000;
    }
}

void Arc::stopSegmentAnimation() {
    if (segmentAnimationActive) {
        animationCounter = -1;
        if (static_cast<int>(currentAnimStep) > static_cast<int>(AnimationStep::GROW_SEGMENT_START_TO_END)) {
            currentAnimStep = AnimationStep::GROW_ARC_TO_SETPOINT;
            stepStartTime = 0;
        }
    }
}

void Arc::updateAnimationStep() {
    int64_t now = esp_timer_get_time() / 1000;
    const int64_t stepDur = 300;
    float p = 0.0f;
    if (currentAnimStep != AnimationStep::NONE && stepStartTime == 0) stepStartTime = now;
    if (stepStartTime > 0) p = constrainf(static_cast<float>(now - stepStartTime) / static_cast<float>(stepDur), 0, 1);

    switch (currentAnimStep) {
        case AnimationStep::SHRINK_ARC_TO_ZERO:
            currentAnimStep = AnimationStep::SHRINK_SEGMENT_TO_START; stepStartTime = 0; break;
        case AnimationStep::SHRINK_SEGMENT_TO_START:
            prevSegmentPercentage = currentSegmentPercentage;
            currentSegmentPercentage = static_cast<int>(100 * (1.0f - p));
            if (prevSegmentPercentage > currentSegmentPercentage)
                clearSegment(currentSegmentPercentage, prevSegmentPercentage);
            if (p >= 1.0f) { currentSegmentPercentage = 0; clearFullSegment();
                             currentAnimStep = AnimationStep::GROW_SEGMENT_START_TO_END; stepStartTime = 0; }
            break;
        case AnimationStep::GROW_SEGMENT_START_TO_END:
            prevSegmentPercentage = currentSegmentPercentage;
            currentSegmentPercentage = static_cast<int>(100 * p);
            if (currentSegmentPercentage > prevSegmentPercentage)
                drawSegment(prevSegmentPercentage, currentSegmentPercentage);
            if (p >= 1.0f) { currentSegmentPercentage = 100;
                             currentAnimStep = AnimationStep::SHRINK_SEGMENT_END_TO_START; stepStartTime = 0; }
            break;
        case AnimationStep::SHRINK_SEGMENT_END_TO_START:
            prevSegmentPercentage = currentSegmentPercentage;
            currentSegmentPercentage = static_cast<int>(100 * (1.0f - p));
            if (prevSegmentPercentage > currentSegmentPercentage)
                clearSegment(currentSegmentPercentage, prevSegmentPercentage);
            if (p >= 1.0f) {
                currentSegmentPercentage = 0; clearFullSegment();
                if (animationCounter < 0) {
                    currentAnimStep = AnimationStep::GROW_ARC_TO_SETPOINT; stepStartTime = 0;
                } else {
                    currentAnimStep = AnimationStep::GROW_SEGMENT_START_TO_END; stepStartTime = 0;
                }
            }
            break;
        case AnimationStep::GROW_ARC_TO_SETPOINT:
            if (stepStartTime == 0) { arcAnimationStartPoint = 0; stepStartTime = now; }
            {
                float tp = constrainf(static_cast<float>(now - stepStartTime) / 500.0f, 0, 1);
                int cur = static_cast<int>(savedSetpoint * tp);
                if (cur > currentPercentage) {
                    float prev_deg = startAngle + constrainf(static_cast<float>(maxArcLength * currentPercentage) / 100.0f, 0, static_cast<float>(maxArcLength) - 0.5f);
                    float cur_deg  = startAngle + constrainf(static_cast<float>(maxArcLength * cur) / 100.0f, 0, static_cast<float>(maxArcLength) - 0.5f);
                    drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                                 prev_deg, cur_deg, arcColor);
                    previousPercentage = currentPercentage;
                    currentPercentage = cur;
                }
                if (tp >= 1.0f) {
                    currentPercentage = targetPercentage = savedSetpoint;
                    segmentAnimationActive = false;
                    currentAnimStep = AnimationStep::NONE;
                }
            }
            break;
        default: break;
    }
}
