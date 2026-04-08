#ifndef ANIMATION_HELPER_H
#define ANIMATION_HELPER_H

#include <Arduino_GFX_Library.h>

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
  static void performTransition(Arduino_GFX* gfx, TransitionType type, uint16_t duration = 300) {
    int width = gfx->width();
    int height = gfx->height();

    switch (type) {
      case FADE:
        // Fade out to black
        for (int i = 0; i < 10; i++) {
          for (int y = 0; y < height; y += 2) {  // Skip every other line for speed
            for (int x = 0; x < width; x += 2) {
              if ((x + y) % 10 == i) {
                gfx->drawPixel(x, y, BLACK);
              }
            }
          }
          delay(duration / 10);
        }
        gfx->fillScreen(BLACK);
        break;

      case WIPE_LEFT:
        for (int x = width; x >= 0; x -= 4) {
          gfx->fillRect(x, 0, 4, height, BLACK);
          delay(duration / (width / 4));
        }
        break;

      case WIPE_RIGHT:
        for (int x = 0; x <= width; x += 4) {
          gfx->fillRect(x, 0, 4, height, BLACK);
          delay(duration / (width / 4));
        }
        break;

      case WIPE_UP:
        for (int y = height; y >= 0; y -= 4) {
          gfx->fillRect(0, y, width, 4, BLACK);
          delay(duration / (height / 4));
        }
        break;

      case WIPE_DOWN:
        for (int y = 0; y <= height; y += 4) {
          gfx->fillRect(0, y, width, 4, BLACK);
          delay(duration / (height / 4));
        }
        break;

      case ZOOM_IN:
        {
          int centerX = width / 2;
          int centerY = height / 2;
          int maxDim = max(width, height);

          for (int r = 0; r <= maxDim / 2; r += 4) {
            // Draw a square that gets smaller
            int x = centerX - r;
            int y = centerY - r;
            int w = r * 2;
            int h = r * 2;

            gfx->drawRect(x, y, w, h, BLACK);
            delay(duration / (maxDim / 8));
          }
          gfx->fillScreen(BLACK);
        }
        break;

      case ZOOM_OUT:
        {
          int centerX = width / 2;
          int centerY = height / 2;
          int maxDim = max(width, height);

          for (int r = 0; r <= maxDim; r += 8) {
            // Draw expanding circles
            gfx->fillCircle(centerX, centerY, r, BLACK);
            delay(duration / (maxDim / 8));
          }
        }
        break;

      case NONE:
      default:
        gfx->fillScreen(BLACK);
        break;
    }
  }
};

#endif  // ANIMATION_HELPER_H