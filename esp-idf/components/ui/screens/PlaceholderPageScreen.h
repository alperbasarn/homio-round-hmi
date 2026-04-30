#pragma once

#include "LvglDisplay.h"
#include "TouchPanel.h"
#include <lvgl.h>
#include <cstdint>
#include <functional>
#include <string>

class PlaceholderPageScreen {
private:
    TouchPanel* touchPanel;
    bool lvglReady;
    bool screenInitialized;
    std::string title;
    std::string subtitle;
    std::function<void()> playPauseAction;

    lv_obj_t* root;
    lv_obj_t* titleLabel;
    lv_obj_t* subtitleLabel;
    lv_obj_t* hintLabel;
    lv_obj_t* playPauseButton;
    lv_obj_t* playPauseIcon;
    bool pcButtonPressed;
    bool pcButtonWasPressed;
    bool pcActionTriggeredThisPress;

    void ensureUi();
    void buildUi();
    void refreshUi(bool forceFullRefresh);
    void destroyUi();
    int scalePx(int referencePx) const;
    bool isPcControlPage() const;
    bool isInsidePlayPauseButton(int x, int y) const;
    void updatePcControlTouch();
    void updatePlayPauseVisual();

public:
    explicit PlaceholderPageScreen(TouchPanel* touch = nullptr);

    void setContent(const std::string& pageTitle, const std::string& pageSubtitle);
    void setPlayPauseAction(std::function<void()> action);
    void activate();
    void deactivate();
    void resetScreen();
    void update();
};
