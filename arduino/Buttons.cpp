#include "Buttons.h"

//////////////////////////////////////////////////
// Base Button Class Implementation
//////////////////////////////////////////////////

Button::Button(Arduino_GFX* graphics, TouchPanel* touch)
  : gfx(graphics), touchPanel(touch), isPressed(false), wasPressed(false),
    isVisible(true), normalColor(WHITE), pressedColor(RED) {}

void Button::initialize(int centerX, int centerY, int buttonWidth, int buttonHeight) {
  x = centerX;
  y = centerY;
  width = buttonWidth;
  height = buttonHeight;
}

void Button::update() {
  // Don't process touches if the button is hidden
  if (!isVisible) {
    isPressed = false;
    wasPressed = false;
    return;
  }

  wasPressed = isPressed;

  // Check if button is being pressed
  if (touchPanel->isPressed()) {
    int touchX = touchPanel->getTouchX();
    int touchY = touchPanel->getTouchY();

    // Check if touch is within button bounds
    if (touchX >= (x - width / 2) && touchX <= (x + width / 2) && touchY >= (y - height / 2) && touchY <= (y + height / 2)) {
      isPressed = true;
    } else {
      isPressed = false;
    }
  } else {
    isPressed = false;
  }
}

void Button::hide() {
  if (isVisible) {
    // Clear the button area
    gfx->fillCircle(x, y, width / 2, BLACK);
    isVisible = false;
  }
}

void Button::unhide() {
  if (!isVisible) {
    isVisible = true;
    draw();  // Redraw the button
  }
}

bool Button::getHasNewPress() {
  return isPressed && !wasPressed;
}

bool Button::getHasNewRelease() {
  return touchPanel->getHasNewRelease() && wasPressed;
}

bool Button::getIsPressed() const {
  return isPressed;
}

bool Button::getIsVisible() const {
  return isVisible;
}

//////////////////////////////////////////////////
// PlayButton Implementation
//////////////////////////////////////////////////

PlayButton::PlayButton(Arduino_GFX* graphics, TouchPanel* touch)
  : Button(graphics, touch) {}

void PlayButton::initialize(int centerX, int centerY, int buttonSize) {
  Button::initialize(centerX, centerY, buttonSize, buttonSize);
}

void PlayButton::draw() {
  if (!isVisible) return;

  uint16_t color = isPressed ? pressedColor : normalColor;

  // Clear the button area first to prevent overdraw
  gfx->fillCircle(x, y, width / 2, BLACK);

  // Draw play triangle
  gfx->fillTriangle(
    x - width / 4, y - height / 3,  // Top left
    x - width / 4, y + height / 3,  // Bottom left
    x + width / 3, y,               // Right point
    color);
}

//////////////////////////////////////////////////
// PauseButton Implementation
//////////////////////////////////////////////////

PauseButton::PauseButton(Arduino_GFX* graphics, TouchPanel* touch)
  : Button(graphics, touch) {}

void PauseButton::initialize(int centerX, int centerY, int buttonSize) {
  Button::initialize(centerX, centerY, buttonSize, buttonSize);
}

void PauseButton::draw() {
  if (!isVisible) return;

  uint16_t color = isPressed ? pressedColor : normalColor;

  // Clear the button area first to prevent overdraw
  gfx->fillCircle(x, y, width / 2, BLACK);

  // Draw pause bars
  int barWidth = width / 5;
  int barHeight = height / 2;
  int spacing = width / 8;

  // Left bar
  gfx->fillRect(x - spacing - barWidth / 2, y - barHeight / 2, barWidth, barHeight, color);
  // Right bar
  gfx->fillRect(x + spacing - barWidth / 2, y - barHeight / 2, barWidth, barHeight, color);
}

//////////////////////////////////////////////////
// RewindButton Implementation
//////////////////////////////////////////////////

RewindButton::RewindButton(Arduino_GFX* graphics, TouchPanel* touch)
  : Button(graphics, touch) {}

void RewindButton::initialize(int centerX, int centerY, int buttonSize) {
  Button::initialize(centerX, centerY, buttonSize, buttonSize);
}

void RewindButton::draw() {
  if (!isVisible) return;

  uint16_t color = isPressed ? pressedColor : normalColor;

  // Clear the button area first to prevent overdraw
  gfx->fillCircle(x, y, width / 2, BLACK);

  // Draw two backward triangles
  int triangleWidth = width / 3;
  int triangleHeight = height / 2;
  int spacing = width / 12;

  // Right triangle
  gfx->fillTriangle(
    x + triangleWidth / 2 - spacing, y,                       // Right point
    x - triangleWidth / 2 - spacing, y - triangleHeight / 2,  // Top left
    x - triangleWidth / 2 - spacing, y + triangleHeight / 2,  // Bottom left
    color);

  // Left triangle
  gfx->fillTriangle(
    x - spacing, y,                                       // Right point
    x - triangleWidth - spacing, y - triangleHeight / 2,  // Top left
    x - triangleWidth - spacing, y + triangleHeight / 2,  // Bottom left
    color);
}

//////////////////////////////////////////////////
// ForwardButton Implementation
//////////////////////////////////////////////////

ForwardButton::ForwardButton(Arduino_GFX* graphics, TouchPanel* touch)
  : Button(graphics, touch) {}

void ForwardButton::initialize(int centerX, int centerY, int buttonSize) {
  Button::initialize(centerX, centerY, buttonSize, buttonSize);
}

void ForwardButton::draw() {
  if (!isVisible) return;

  uint16_t color = isPressed ? pressedColor : normalColor;

  // Clear the button area first to prevent overdraw
  gfx->fillCircle(x, y, width / 2, BLACK);

  // Draw two forward triangles
  int triangleWidth = width / 3;
  int triangleHeight = height / 2;
  int spacing = width / 12;

  // Left triangle
  gfx->fillTriangle(
    x - triangleWidth / 2 + spacing, y,                       // Left point
    x + triangleWidth / 2 + spacing, y - triangleHeight / 2,  // Top right
    x + triangleWidth / 2 + spacing, y + triangleHeight / 2,  // Bottom right
    color);

  // Right triangle
  gfx->fillTriangle(
    x + spacing, y,                                       // Left point
    x + triangleWidth + spacing, y - triangleHeight / 2,  // Top right
    x + triangleWidth + spacing, y + triangleHeight / 2,  // Bottom right
    color);
}

//////////////////////////////////////////////////
// BackButton Implementation
//////////////////////////////////////////////////

BackButton::BackButton(Arduino_GFX* graphics, TouchPanel* touch)
  : Button(graphics, touch) {
  // Use gray for normal color instead of white
  normalColor = graphics->color565(128, 128, 128);  // Gray
}

void BackButton::initialize(int centerX, int centerY, int buttonWidth, int buttonHeight) {
  Button::initialize(centerX, centerY, buttonWidth, buttonHeight);
}

void BackButton::draw() {
  if (!isVisible) return;

  uint16_t bgColor = isPressed ? pressedColor : normalColor;

  // Clear area and draw elliptical button
  gfx->fillEllipse(x, y, width, height, BLACK);
  gfx->fillEllipse(x, y, width, height, bgColor);

  // Draw the arrow inside the back button (always white)
  gfx->fillTriangle(
    x - 5, y,       // Left point
    x + 5, y - 10,  // Top right
    x + 5, y + 10,  // Bottom right
    WHITE);
}