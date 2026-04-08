#pragma once

#include "LGFX_Config.hpp"
#include "TouchPanel.h"
#include <cstdint>

// Base Button class
class Button {
protected:
    LGFX* gfx;
    TouchPanel* touchPanel;
    int x, y;
    int width, height;
    bool isPressed;
    bool wasPressed;
    bool isVisible;
    uint16_t normalColor;
    uint16_t pressedColor;

public:
    Button(LGFX* graphics, TouchPanel* touch);
    virtual ~Button() = default;

    virtual void initialize(int centerX, int centerY, int buttonWidth, int buttonHeight);
    virtual void update();
    virtual void draw() = 0;

    void hide();
    void unhide();

    bool getHasNewPress();
    bool getHasNewRelease();
    bool getIsPressed() const { return isPressed; }
    bool getIsVisible() const { return isVisible; }
};

// Play Button
class PlayButton : public Button {
public:
    PlayButton(LGFX* graphics, TouchPanel* touch);
    void initialize(int centerX, int centerY, int buttonSize);
    void draw() override;
};

// Pause Button
class PauseButton : public Button {
public:
    PauseButton(LGFX* graphics, TouchPanel* touch);
    void initialize(int centerX, int centerY, int buttonSize);
    void draw() override;
};

// Rewind Button
class RewindButton : public Button {
public:
    RewindButton(LGFX* graphics, TouchPanel* touch);
    void initialize(int centerX, int centerY, int buttonSize);
    void draw() override;
};

// Forward Button
class ForwardButton : public Button {
public:
    ForwardButton(LGFX* graphics, TouchPanel* touch);
    void initialize(int centerX, int centerY, int buttonSize);
    void draw() override;
};

// Back Button
class BackButton : public Button {
public:
    BackButton(LGFX* graphics, TouchPanel* touch);
    void initialize(int centerX, int centerY, int buttonWidth, int buttonHeight);
    void draw() override;
};
