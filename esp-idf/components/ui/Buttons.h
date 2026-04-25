#pragma once

#include "LvglDisplay.h"
#include "TouchPanel.h"
#include <cstdint>

class Button {
protected:
    TouchPanel* touchPanel;
    int x, y;
    int width, height;
    bool isPressed;
    bool wasPressed;
    bool isVisible;
    uint32_t normalColor;   // RGB888
    uint32_t pressedColor;  // RGB888

public:
    Button(TouchPanel* touch);
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

class PlayButton : public Button {
public:
    PlayButton(TouchPanel* touch);
    void initialize(int centerX, int centerY, int buttonSize);
    void draw() override;
};

class PauseButton : public Button {
public:
    PauseButton(TouchPanel* touch);
    void initialize(int centerX, int centerY, int buttonSize);
    void draw() override;
};

class RewindButton : public Button {
public:
    RewindButton(TouchPanel* touch);
    void initialize(int centerX, int centerY, int buttonSize);
    void draw() override;
};

class ForwardButton : public Button {
public:
    ForwardButton(TouchPanel* touch);
    void initialize(int centerX, int centerY, int buttonSize);
    void draw() override;
};

class BackButton : public Button {
public:
    BackButton(TouchPanel* touch);
    void initialize(int centerX, int centerY, int buttonWidth, int buttonHeight);
    void draw() override;
};
