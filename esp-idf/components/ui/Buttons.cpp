#include "Buttons.h"

//////////////////////////////////////////////////
// Base Button Class Implementation
//////////////////////////////////////////////////

Button::Button(LGFX* graphics, TouchPanel* touch)
    : gfx(graphics), touchPanel(touch), x(0), y(0), width(0), height(0),
      isPressed(false), wasPressed(false), isVisible(true),
      normalColor(TFT_WHITE), pressedColor(TFT_RED) {}

void Button::initialize(int centerX, int centerY, int buttonWidth, int buttonHeight) {
    x = centerX;
    y = centerY;
    width = buttonWidth;
    height = buttonHeight;
}

void Button::update() {
    if (!isVisible) {
        isPressed = false;
        wasPressed = false;
        return;
    }

    wasPressed = isPressed;

    if (touchPanel->isPressed()) {
        int touchX = touchPanel->getTouchX();
        int touchY = touchPanel->getTouchY();

        if (touchX >= (x - width / 2) && touchX <= (x + width / 2) &&
            touchY >= (y - height / 2) && touchY <= (y + height / 2)) {
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
        gfx->fillCircle(x, y, width / 2, TFT_BLACK);
        isVisible = false;
    }
}

void Button::unhide() {
    if (!isVisible) {
        isVisible = true;
        draw();
    }
}

bool Button::getHasNewPress() {
    return isPressed && !wasPressed;
}

bool Button::getHasNewRelease() {
    return touchPanel->getHasNewRelease() && wasPressed;
}

//////////////////////////////////////////////////
// PlayButton Implementation
//////////////////////////////////////////////////

PlayButton::PlayButton(LGFX* graphics, TouchPanel* touch)
    : Button(graphics, touch) {}

void PlayButton::initialize(int centerX, int centerY, int buttonSize) {
    Button::initialize(centerX, centerY, buttonSize, buttonSize);
}

void PlayButton::draw() {
    if (!isVisible) return;

    uint16_t color = isPressed ? pressedColor : normalColor;
    gfx->fillCircle(x, y, width / 2, TFT_BLACK);

    // Draw play triangle
    gfx->fillTriangle(
        x - width / 4, y - height / 3,
        x - width / 4, y + height / 3,
        x + width / 3, y,
        color);
}

//////////////////////////////////////////////////
// PauseButton Implementation
//////////////////////////////////////////////////

PauseButton::PauseButton(LGFX* graphics, TouchPanel* touch)
    : Button(graphics, touch) {}

void PauseButton::initialize(int centerX, int centerY, int buttonSize) {
    Button::initialize(centerX, centerY, buttonSize, buttonSize);
}

void PauseButton::draw() {
    if (!isVisible) return;

    uint16_t color = isPressed ? pressedColor : normalColor;
    gfx->fillCircle(x, y, width / 2, TFT_BLACK);

    int barWidth = width / 5;
    int barHeight = height / 2;
    int spacing = width / 8;

    gfx->fillRect(x - spacing - barWidth / 2, y - barHeight / 2, barWidth, barHeight, color);
    gfx->fillRect(x + spacing - barWidth / 2, y - barHeight / 2, barWidth, barHeight, color);
}

//////////////////////////////////////////////////
// RewindButton Implementation
//////////////////////////////////////////////////

RewindButton::RewindButton(LGFX* graphics, TouchPanel* touch)
    : Button(graphics, touch) {}

void RewindButton::initialize(int centerX, int centerY, int buttonSize) {
    Button::initialize(centerX, centerY, buttonSize, buttonSize);
}

void RewindButton::draw() {
    if (!isVisible) return;

    uint16_t color = isPressed ? pressedColor : normalColor;
    gfx->fillCircle(x, y, width / 2, TFT_BLACK);

    int triangleWidth = width / 3;
    int triangleHeight = height / 2;
    int spacing = width / 12;

    // Right triangle
    gfx->fillTriangle(
        x + triangleWidth / 2 - spacing, y,
        x - triangleWidth / 2 - spacing, y - triangleHeight / 2,
        x - triangleWidth / 2 - spacing, y + triangleHeight / 2,
        color);

    // Left triangle
    gfx->fillTriangle(
        x - spacing, y,
        x - triangleWidth - spacing, y - triangleHeight / 2,
        x - triangleWidth - spacing, y + triangleHeight / 2,
        color);
}

//////////////////////////////////////////////////
// ForwardButton Implementation
//////////////////////////////////////////////////

ForwardButton::ForwardButton(LGFX* graphics, TouchPanel* touch)
    : Button(graphics, touch) {}

void ForwardButton::initialize(int centerX, int centerY, int buttonSize) {
    Button::initialize(centerX, centerY, buttonSize, buttonSize);
}

void ForwardButton::draw() {
    if (!isVisible) return;

    uint16_t color = isPressed ? pressedColor : normalColor;
    gfx->fillCircle(x, y, width / 2, TFT_BLACK);

    int triangleWidth = width / 3;
    int triangleHeight = height / 2;
    int spacing = width / 12;

    // Left triangle
    gfx->fillTriangle(
        x - triangleWidth / 2 + spacing, y,
        x + triangleWidth / 2 + spacing, y - triangleHeight / 2,
        x + triangleWidth / 2 + spacing, y + triangleHeight / 2,
        color);

    // Right triangle
    gfx->fillTriangle(
        x + spacing, y,
        x + triangleWidth + spacing, y - triangleHeight / 2,
        x + triangleWidth + spacing, y + triangleHeight / 2,
        color);
}

//////////////////////////////////////////////////
// BackButton Implementation
//////////////////////////////////////////////////

BackButton::BackButton(LGFX* graphics, TouchPanel* touch)
    : Button(graphics, touch) {
    normalColor = gfx->color565(128, 128, 128);  // Gray
}

void BackButton::initialize(int centerX, int centerY, int buttonWidth, int buttonHeight) {
    Button::initialize(centerX, centerY, buttonWidth, buttonHeight);
}

void BackButton::draw() {
    if (!isVisible) return;

    uint16_t bgColor = isPressed ? pressedColor : normalColor;

    gfx->fillEllipse(x, y, width, height, TFT_BLACK);
    gfx->fillEllipse(x, y, width, height, bgColor);

    // Draw arrow
    gfx->fillTriangle(
        x - 5, y,
        x + 5, y - 10,
        x + 5, y + 10,
        TFT_WHITE);
}
