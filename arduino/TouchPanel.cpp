#include "TouchPanel.h"

TouchPanel::TouchPanel(int sda, int scl, int rst, int irq)
  : touch(sda, scl, rst, irq), pressDetected(false), _pressDetected(false),
    pressed(false), released(true), hold(false), displayOn(true),
    pressTime(0), releaseTime(0), pressDuration(0),
    holdThreshold(1000), longHoldThreshold(10000), releaseThreshold(100), touchX(0), touchY(0) {}

void TouchPanel::initialize() {
  pressTime = millis();
  touch.begin();
}

void TouchPanel::handleTouchPanel() {
  int currentTime = millis();
  hasNewPress = false;
  hasNewRelease = false;
  hasNewHoldRelease = false;
  if (touch.available()) {
    if (!firstPress) {
      touchX = touch.data.x;
      touchY = touch.data.y;
      pressDetected = true;
      if (!pressed) {
        hasNewPress = true;
        pressed = true;
        released = false;
        pressTime = currentTime;
        //Serial.println("Pressed");
      } else if (!hold && currentTime - pressTime > holdThreshold) {
        if (!longHold && currentTime - pressTime > longHoldThreshold) {
          longHold = true;
          //Serial.println("long hold");
        }
        hold = true;
        //Serial.println("Hold");
      }
    } else {
      firstPress = false;
    }

  } else {
    pressDetected = false;
    if (_pressDetected) {
      releaseTime = currentTime;
    }

    if (!released && currentTime - releaseTime > releaseThreshold) {
      if (hold) {
        hasNewHoldRelease = true;
      }
      longHold = false;
      hold = false;
      pressed = false;
      if (firstRelease) firstRelease = false;
      else {
        released = true;
        hasNewRelease = true;
        //Serial.println("Released");
      }
    }
  }

  _pressDetected = pressDetected;

  int releaseExpireDuration = 10;
}

bool TouchPanel::isPressed() const {
  return pressed;
}

bool TouchPanel::getHasNewPress() const {
  return hasNewPress;
}

bool TouchPanel::isHold() const {
  return hold;
}

bool TouchPanel::isLongHold() const {
  return longHold;
}

bool TouchPanel::isReleased() const {
  return released;
}

bool TouchPanel::getHasNewRelease() const {
  return hasNewRelease;
}

bool TouchPanel::getHasNewHoldRelease() const {
  return hasNewHoldRelease;
}

int TouchPanel::getTouchX() const {
  return touchX;
}

int TouchPanel::getTouchY() const {
  return touchY;
}
