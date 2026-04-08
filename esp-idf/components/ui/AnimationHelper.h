#pragma once

#include "LGFX_Config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>

class AnimationHelper {
public:
    // Transition types
    enum TransitionType {
        NONE,
        FADE,
        WIPE_LEFT,
        WIPE_RIGHT,
        WIPE_UP,
        WIPE_DOWN,
        ZOOM_IN,
        ZOOM_OUT
    };

    // Static method to perform screen transitions
    static void performTransition(LGFX* gfx, TransitionType type, uint16_t duration = 300) {
        int width = gfx->width();
        int height = gfx->height();

        switch (type) {
            case FADE:
                // Fade out to black using dither pattern
                for (int i = 0; i < 10; i++) {
                    for (int y = 0; y < height; y += 2) {  // Skip every other line for speed
                        for (int x = 0; x < width; x += 2) {
                            if ((x + y) % 10 == i) {
                                gfx->drawPixel(x, y, TFT_BLACK);
                            }
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(duration / 10));
                }
                gfx->fillScreen(TFT_BLACK);
                break;

            case WIPE_LEFT:
                for (int x = width; x >= 0; x -= 4) {
                    gfx->fillRect(x, 0, 4, height, TFT_BLACK);
                    vTaskDelay(pdMS_TO_TICKS(duration / (width / 4)));
                }
                break;

            case WIPE_RIGHT:
                for (int x = 0; x <= width; x += 4) {
                    gfx->fillRect(x, 0, 4, height, TFT_BLACK);
                    vTaskDelay(pdMS_TO_TICKS(duration / (width / 4)));
                }
                break;

            case WIPE_UP:
                for (int y = height; y >= 0; y -= 4) {
                    gfx->fillRect(0, y, width, 4, TFT_BLACK);
                    vTaskDelay(pdMS_TO_TICKS(duration / (height / 4)));
                }
                break;

            case WIPE_DOWN:
                for (int y = 0; y <= height; y += 4) {
                    gfx->fillRect(0, y, width, 4, TFT_BLACK);
                    vTaskDelay(pdMS_TO_TICKS(duration / (height / 4)));
                }
                break;

            case ZOOM_IN:
                {
                    int centerX = width / 2;
                    int centerY = height / 2;
                    int maxDim = std::max(width, height);

                    for (int r = 0; r <= maxDim / 2; r += 4) {
                        // Draw a square that gets smaller
                        int x = centerX - r;
                        int y = centerY - r;
                        int w = r * 2;
                        int h = r * 2;

                        gfx->drawRect(x, y, w, h, TFT_BLACK);
                        vTaskDelay(pdMS_TO_TICKS(duration / (maxDim / 8)));
                    }
                    gfx->fillScreen(TFT_BLACK);
                }
                break;

            case ZOOM_OUT:
                {
                    int centerX = width / 2;
                    int centerY = height / 2;
                    int maxDim = std::max(width, height);

                    for (int r = 0; r <= maxDim; r += 8) {
                        // Draw expanding circles
                        gfx->fillCircle(centerX, centerY, r, TFT_BLACK);
                        vTaskDelay(pdMS_TO_TICKS(duration / (maxDim / 8)));
                    }
                }
                break;

            case NONE:
            default:
                gfx->fillScreen(TFT_BLACK);
                break;
        }
    }

    // Easing functions for smooth animations
    static float easeInOut(float t) {
        return t < 0.5f ? 2.0f * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
    }

    static float easeIn(float t) {
        return t * t;
    }

    static float easeOut(float t) {
        return 1.0f - (1.0f - t) * (1.0f - t);
    }

    // Interpolate between two values
    static int lerp(int start, int end, float t) {
        return start + static_cast<int>((end - start) * t);
    }

    // Interpolate colors
    static uint16_t lerpColor(uint16_t color1, uint16_t color2, float t) {
        // Extract RGB components (RGB565)
        uint8_t r1 = (color1 >> 11) & 0x1F;
        uint8_t g1 = (color1 >> 5) & 0x3F;
        uint8_t b1 = color1 & 0x1F;

        uint8_t r2 = (color2 >> 11) & 0x1F;
        uint8_t g2 = (color2 >> 5) & 0x3F;
        uint8_t b2 = color2 & 0x1F;

        // Interpolate
        uint8_t r = r1 + static_cast<uint8_t>((r2 - r1) * t);
        uint8_t g = g1 + static_cast<uint8_t>((g2 - g1) * t);
        uint8_t b = b1 + static_cast<uint8_t>((b2 - b1) * t);

        return (r << 11) | (g << 5) | b;
    }
};
