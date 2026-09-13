#pragma once
// LightScreenController (CTRL-22 + LED-05 + the screen half of LED-04): the LIGHT screen on the
// registry. Tap on the disc toggles power, the knob moves brightness, a swipe steps the effect, a
// drag on the rim sets the hue from the angle, a tap on a rim segment selects that strip, a 3 s
// hold on an unpaired segment starts pairing, a 3 s hold anywhere else (after no drag) goes back
// to INFO. While a pairing is requested the knob dials the blink count and a tap confirms it.
// Every control reaches the models only (records through the queue); the view renders the state.
#include <cstdint>

#include "IScreenView.h"
#include "LightScreenState.h"
#include "ScreenController.h"
#include "input/InputEvent.h"
#include "model/AppModel.h"

namespace controllers {

class LightScreenController final : public ScreenController {
public:
    static constexpr int kDragThresholdPx = 6;

    LightScreenController(model::AppModel& app, ui::ScreenView<ui::LightScreenState>* view, int panelWidthPx,
                          int panelHeightPx);

    model::AppScreen screen() const override { return model::AppScreen::Light; }
    void onEnter(uint32_t nowMs) override;
    void onExit(uint32_t nowMs) override;
    bool onInput(const input::InputEvent& event, uint32_t nowMs) override;
    void tick(uint32_t nowMs) override;

    int dial() const { return dial_; }
    bool dragging() const { return dragged_; }

private:
    void post(const model::ModelUpdate& record);
    int stripUnderRim(int x, int y) const;
    bool pairingRequested() const;

    model::AppModel& app_;
    ui::ScreenView<ui::LightScreenState>* view_;
    int panelWidthPx_;
    int panelHeightPx_;
    input::HitZone pressZone_ = input::HitZone::Dead;
    int pressX_ = 0;
    int pressY_ = 0;
    bool touchDown_ = false;
    bool dragged_ = false;
    int dial_ = 0;
};

}  // namespace controllers
