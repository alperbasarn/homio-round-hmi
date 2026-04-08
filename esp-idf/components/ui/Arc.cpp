#include "Arc.h"
#include "esp_timer.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Arc::Arc(LGFX* graphics)
    : gfx(graphics), centerX(0), centerY(0), radius(0), arcWidth(0),
      maxArcLength(0), startAngle(0), currentPercentage(0), previousPercentage(0),
      targetPercentage(0), animationSpeed(5), arcColor(TFT_WHITE), backgroundColor(TFT_BLACK),
      segmentVisible(true), currentSegmentPercentage(100),
      prevSegmentPercentage(100), segmentColor(0x4208),
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

// Custom thick arc drawing function that works reliably on QSPI panels
void Arc::drawThickArc(int cx, int cy, int outerR, int innerR,
                        float startAngle, float endAngle, uint16_t color) {
    // Convert to radians
    float startRad = startAngle * M_PI / 180.0f;
    float endRad = endAngle * M_PI / 180.0f;

    // Handle angle wrapping
    if (endRad < startRad) {
        endRad += 2.0f * M_PI;
    }

    // Calculate step size based on outer radius for smooth appearance
    float angleStep = 0.3f * M_PI / 180.0f;

    // Draw filled arc using radial lines
    for (float angle = startRad; angle <= endRad; angle += angleStep) {
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        int x1 = cx + static_cast<int>(innerR * cosA);
        int y1 = cy - static_cast<int>(innerR * sinA);
        int x2 = cx + static_cast<int>(outerR * cosA);
        int y2 = cy - static_cast<int>(outerR * sinA);

        gfx->drawLine(x1, y1, x2, y2, color);
    }

    // Draw the final line at endAngle
    float cosEnd = std::cos(endRad);
    float sinEnd = std::sin(endRad);
    int x1 = cx + static_cast<int>(innerR * cosEnd);
    int y1 = cy - static_cast<int>(innerR * sinEnd);
    int x2 = cx + static_cast<int>(outerR * cosEnd);
    int y2 = cy - static_cast<int>(outerR * sinEnd);
    gfx->drawLine(x1, y1, x2, y2, color);
}

void Arc::initialize(int cx, int cy, int rad, int width, int length, int start) {
    centerX = cx;
    centerY = cy;
    radius = rad;
    arcWidth = width;
    maxArcLength = length;
    startAngle = start;
    currentPercentage = 0;
    previousPercentage = 0;
    targetPercentage = 0;
    segmentVisible = true;
    currentSegmentPercentage = 0;
    prevSegmentPercentage = 0;
    segmentAnimationActive = false;
    currentAnimStep = AnimationStep::NONE;
    savedSetpoint = 0;
    animationCounter = 0;
    animationStartPoint = 0;
    animationEndPoint = 100;
    arcAnimationStartTime = 0;

    drawFullSegment();
}

void Arc::update() {
    if (segmentAnimationActive) {
        updateAnimationStep();
        return;
    }

    int64_t currentMillis = esp_timer_get_time() / 1000;

    if (currentPercentage != targetPercentage && arcAnimationStartTime == 0) {
        previousPercentage = currentPercentage;
        arcAnimationStartTime = currentMillis;
        arcAnimationStartPoint = currentPercentage;
    }

    if (arcAnimationStartTime > 0) {
        float progress = constrainf((float)(currentMillis - arcAnimationStartTime) / (float)arcAnimationDuration, 0.0f, 1.0f);
        currentPercentage = arcAnimationStartPoint + (targetPercentage - arcAnimationStartPoint) * progress;
        draw();

        if (progress >= 1.0f) {
            currentPercentage = targetPercentage;
            arcAnimationStartTime = 0;
        }
    }
}

void Arc::draw() {
    if (currentPercentage != previousPercentage) {
        float currentArcLength = constrainf((maxArcLength * currentPercentage) / 100.0f, 0.0f, maxArcLength - 0.5f);
        float previousArcLength = constrainf((maxArcLength * previousPercentage) / 100.0f, 0.0f, maxArcLength - 0.5f);

        if (currentPercentage > previousPercentage) {
            if (startAngle + previousArcLength != startAngle + currentArcLength) {
                drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                            startAngle + previousArcLength, startAngle + currentArcLength, arcColor);
            }
        } else if (currentPercentage < previousPercentage) {
            if (startAngle + currentArcLength != startAngle + previousArcLength) {
                drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                            startAngle + currentArcLength, startAngle + previousArcLength, segmentColor);
            }
        }
        previousPercentage = currentPercentage;
    }
}

void Arc::drawSegment(int startPercent, int endPercent) {
    startPercent = constrain(startPercent, 0, 100);
    endPercent = constrain(endPercent, 0, 100);

    float startDeg = startAngle + constrainf((maxArcLength * startPercent) / 100.0f, 0.0f, maxArcLength - 0.5f);
    float endDeg = startAngle + constrainf((maxArcLength * endPercent) / 100.0f, 0.0f, maxArcLength - 0.5f);

    if (startDeg < endDeg) {
        drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                    startDeg, endDeg, segmentColor);
    }
}

void Arc::drawFullSegment() {
    drawSegment(0, 100);
}

void Arc::clearSegment(int startPercent, int endPercent) {
    startPercent = constrain(startPercent, 0, 100);
    endPercent = constrain(endPercent, 0, 100);

    float startDeg = startAngle + constrainf((maxArcLength * startPercent) / 100.0f, 0.0f, maxArcLength - 0.5f);
    float endDeg = startAngle + constrainf((maxArcLength * endPercent) / 100.0f, 0.0f, maxArcLength - 0.5f);

    if (startDeg < endDeg) {
        drawThickArc(centerX, centerY, radius + arcWidth/2 + 1, radius - arcWidth/2 - 1,
                    startDeg, endDeg, backgroundColor);
    }
}

void Arc::clearFullSegment() {
    drawThickArc(centerX, centerY, radius + arcWidth/2 + 2, radius - arcWidth/2 - 2,
                 startAngle - 2, startAngle + maxArcLength + 2, backgroundColor);
}

void Arc::clearArc() {
    drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                 startAngle, startAngle + maxArcLength, segmentColor);
}

void Arc::setColor(uint16_t color) {
    arcColor = color;
    segmentColor = (arcColor & 0xF7DE) >> 1;

    if (currentPercentage > 0) {
        drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                    startAngle, startAngle + constrainf((maxArcLength * currentPercentage) / 100.0f, 0.0f, maxArcLength - 0.5f), arcColor);
    }
}

void Arc::setPercentage(int percentage) {
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;

    if (!segmentAnimationActive) {
        if (targetPercentage != percentage) {
            targetPercentage = percentage;
            arcAnimationStartTime = 0;
        }
    } else {
        savedSetpoint = percentage;
    }
}

void Arc::reset() {
    currentPercentage = 0;
    previousPercentage = 0;
    targetPercentage = 0;
    arcAnimationStartTime = 0;
    clearFullSegment();
    segmentAnimationActive = false;
    currentSegmentPercentage = 0;
    prevSegmentPercentage = 0;

    // Animate segment drawing
    int64_t startTime = esp_timer_get_time() / 1000;
    int64_t animDuration = 500;
    int lastSegmentEnd = 0;

    while (true) {
        int64_t currentTime = esp_timer_get_time() / 1000;
        float progress = constrainf((float)(currentTime - startTime) / (float)animDuration, 0.0f, 1.0f);
        int segmentEnd = progress * 100;

        if (segmentEnd > lastSegmentEnd) {
            drawSegment(lastSegmentEnd, segmentEnd);
            lastSegmentEnd = segmentEnd;
        }

        if (progress >= 1.0f) break;
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
        currentPercentage = 0;
        previousPercentage = 0;
        currentSegmentPercentage = 100;
        prevSegmentPercentage = 100;
        animationCounter = 0;
        animationStartPoint = 0;
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
    int64_t currentMillis = esp_timer_get_time() / 1000;
    float animationProgress = 0.0f;
    const int64_t stepDuration = 300;

    if (currentAnimStep != AnimationStep::NONE && stepStartTime == 0) {
        stepStartTime = currentMillis;
    }

    if (stepStartTime > 0) {
        animationProgress = constrainf((float)(currentMillis - stepStartTime) / (float)stepDuration, 0.0f, 1.0f);
    }

    switch (currentAnimStep) {
        case AnimationStep::SHRINK_ARC_TO_ZERO:
            currentAnimStep = AnimationStep::SHRINK_SEGMENT_TO_START;
            stepStartTime = 0;
            break;

        case AnimationStep::SHRINK_SEGMENT_TO_START:
            prevSegmentPercentage = currentSegmentPercentage;
            currentSegmentPercentage = 100 * (1.0f - animationProgress);

            if (prevSegmentPercentage > currentSegmentPercentage) {
                clearSegment(currentSegmentPercentage, prevSegmentPercentage);
            }

            if (animationProgress >= 1.0f) {
                currentSegmentPercentage = 0;
                clearFullSegment();
                currentAnimStep = AnimationStep::GROW_SEGMENT_START_TO_END;
                stepStartTime = 0;
            }
            break;

        case AnimationStep::GROW_SEGMENT_START_TO_END:
            prevSegmentPercentage = currentSegmentPercentage;
            currentSegmentPercentage = 100 * animationProgress;

            if (animationCounter == -1) {
                if (currentSegmentPercentage > prevSegmentPercentage) {
                    drawSegment(prevSegmentPercentage, currentSegmentPercentage);
                }
                previousPercentage = currentPercentage;
                currentPercentage = savedSetpoint * animationProgress;

                if (currentPercentage != previousPercentage) {
                    float currentArcLength = constrainf((maxArcLength * currentPercentage) / 100.0f, 0.0f, maxArcLength - 0.5f);
                    float previousArcLength = constrainf((maxArcLength * previousPercentage) / 100.0f, 0.0f, maxArcLength - 0.5f);

                    if (startAngle + previousArcLength != startAngle + currentArcLength) {
                        drawThickArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                                    startAngle + previousArcLength, startAngle + currentArcLength, arcColor);
                    }
                }
            } else {
                if (currentSegmentPercentage > prevSegmentPercentage) {
                    drawSegment(prevSegmentPercentage, currentSegmentPercentage);
                }
            }

            if (animationProgress >= 1.0f) {
                if (animationCounter == -1) {
                    segmentAnimationActive = false;
                    currentAnimStep = AnimationStep::NONE;
                } else {
                    currentAnimStep = AnimationStep::SHRINK_SEGMENT_START_TO_END;
                }
                stepStartTime = 0;
            }
            break;

        case AnimationStep::SHRINK_SEGMENT_START_TO_END:
            prevSegmentPercentage = currentSegmentPercentage;
            currentSegmentPercentage = 100 * (1.0f - animationProgress);

            if (prevSegmentPercentage > currentSegmentPercentage) {
                clearSegment(currentSegmentPercentage, prevSegmentPercentage);
            }

            if (animationProgress >= 1.0f) {
                currentSegmentPercentage = 0;
                clearFullSegment();
                currentAnimStep = AnimationStep::GROW_SEGMENT_END_TO_START;
                currentSegmentPercentage = 0;
                stepStartTime = 0;
            }
            break;

        case AnimationStep::GROW_SEGMENT_END_TO_START:
            prevSegmentPercentage = currentSegmentPercentage;
            currentSegmentPercentage = 100 * animationProgress;

            if (currentSegmentPercentage > prevSegmentPercentage) {
                int prevStart = 100 - prevSegmentPercentage;
                int currentStart = 100 - currentSegmentPercentage;
                drawSegment(currentStart, prevStart);
            }

            if (animationProgress >= 1.0f) {
                currentAnimStep = AnimationStep::SHRINK_SEGMENT_END_TO_START;
                stepStartTime = 0;
            }
            break;

        case AnimationStep::SHRINK_SEGMENT_END_TO_START:
            prevSegmentPercentage = currentSegmentPercentage;
            currentSegmentPercentage = 100 * (1.0f - animationProgress);

            if (prevSegmentPercentage > currentSegmentPercentage) {
                int prevStart = 100 - prevSegmentPercentage;
                int currentStart = 100 - currentSegmentPercentage;
                clearSegment(prevStart, currentStart);
            }

            if (animationProgress >= 1.0f) {
                currentSegmentPercentage = 0;
                clearFullSegment();

                if (animationCounter == -1) {
                    currentAnimStep = AnimationStep::GROW_ARC_TO_SETPOINT;
                } else {
                    currentAnimStep = AnimationStep::GROW_SEGMENT_START_TO_END;
                    currentSegmentPercentage = 0;
                    animationCounter++;
                }
                stepStartTime = 0;
            }
            break;

        case AnimationStep::GROW_ARC_TO_SETPOINT:
            drawFullSegment();
            previousPercentage = 0;
            currentPercentage = 0;
            targetPercentage = savedSetpoint;
            arcAnimationStartTime = currentMillis;
            arcAnimationStartPoint = 0;
            segmentAnimationActive = false;
            currentAnimStep = AnimationStep::NONE;
            stepStartTime = 0;
            break;

        case AnimationStep::NONE:
        default:
            break;
    }
}
