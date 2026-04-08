#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino_GFX_Library.h>
#include "TouchPanel.h"

// Base Button class
class Button {
protected:
  Arduino_GFX* gfx;
  TouchPanel* touchPanel;
  int x, y;                  // Center position
  int width, height;         // Button dimensions
  bool isPressed;            // Current press state
  bool wasPressed;           // Previous press state
  bool isVisible;            // Visibility state
  uint16_t normalColor;      // Color when not pressed
  uint16_t pressedColor;     // Color when pressed

public:
  Button(Arduino_GFX* graphics, TouchPanel* touch);
  
  virtual void initialize(int centerX, int centerY, int buttonWidth, int buttonHeight);
  virtual void update();
  virtual void draw() = 0;   // Pure virtual, must be implemented by derived classes
  
  void hide();               // Hide the button (clear its area)
  void unhide();             // Make the button visible again
  
  bool getHasNewPress();     // Returns true if button was just pressed
  bool getHasNewRelease();   // Returns true if button was just released
  bool getIsPressed() const; // Returns current press state
  bool getIsVisible() const; // Returns current visibility state
};

// Play Button class
class PlayButton : public Button {
public:
  PlayButton(Arduino_GFX* graphics, TouchPanel* touch);
  
  void initialize(int centerX, int centerY, int buttonSize);
  void draw() override;
};

// Pause Button class
class PauseButton : public Button {
public:
  PauseButton(Arduino_GFX* graphics, TouchPanel* touch);
  
  void initialize(int centerX, int centerY, int buttonSize);
  void draw() override;
};

// Rewind Button class
class RewindButton : public Button {
public:
  RewindButton(Arduino_GFX* graphics, TouchPanel* touch);
  
  void initialize(int centerX, int centerY, int buttonSize);
  void draw() override;
};

// Forward Button class
class ForwardButton : public Button {
public:
  ForwardButton(Arduino_GFX* graphics, TouchPanel* touch);
  
  void initialize(int centerX, int centerY, int buttonSize);
  void draw() override;
};

// Back Button class
class BackButton : public Button {
public:
  BackButton(Arduino_GFX* graphics, TouchPanel* touch);
  
  void initialize(int centerX, int centerY, int buttonWidth, int buttonHeight);
  void draw() override;
};

#endif // BUTTONS_H