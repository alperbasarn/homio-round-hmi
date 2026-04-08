#ifndef TOUCH_PANEL_H
#define TOUCH_PANEL_H

#include <Arduino.h>
#include <CST816S.h>  // Include the touch screen library

class TouchPanel {
private:
  CST816S touch;
  bool firstPress = true;
  bool firstRelease = true;
  bool pressDetected;
  bool _pressDetected;

  bool pressed;
  bool released;
  bool hold;
  bool hasNewPress;
  bool hasNewRelease;
  bool hasNewHoldRelease;
  bool longHold;

  bool displayOn;
  int pressTime;         // Time when press detected
  int releaseTime;       // Time when release detected
  int pressDuration;     // Actual duration of press event
  int holdThreshold;     // Threshold duration to determine hold
  int pressThreshold;
  int longHoldThreshold;     // Threshold duration to determine hold
  int releaseThreshold;  // Threshold duration to determine release
  int touchX;            // X-coordinate of touch
  int touchY;            // Y-coordinate of touch

public:
  TouchPanel(int sda, int scl, int rst, int irq);
  void initialize();
  void handleTouchPanel();
  bool isPressed() const;
  bool isHold() const;
  bool isLongHold() const;
  bool isReleased() const;
  bool getHasNewPress() const;
  bool getHasNewRelease() const;
  bool getHasNewHoldRelease() const;
  int getTouchX() const;
  int getTouchY() const;
};

#endif  // TOUCH_PANEL_H
