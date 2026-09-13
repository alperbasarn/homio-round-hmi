#include "LightScreenController.h"

namespace controllers {

using input::Gesture;
using input::HitZone;
using input::InputType;

LightScreenController::LightScreenController(model::AppModel& app, ui::ScreenView<ui::LightScreenState>* view,
                                             int panelWidthPx, int panelHeightPx)
    : app_(app), view_(view), panelWidthPx_(panelWidthPx), panelHeightPx_(panelHeightPx) {}

void LightScreenController::post(const model::ModelUpdate& record) {
    app_.mutations().post(record);
}

bool LightScreenController::pairingRequested() const {
    return app_.ledRegistry().state().pairing == model::LedPairing::Requested;
}

int LightScreenController::stripUnderRim(int x, int y) const {
    const model::LedRegistryState& s = app_.ledRegistry().state();
    if (s.used == 0) {
        return -1;
    }
    const int ordinal = ui::rim::segmentAt(ui::rim::angleDeg(x, y, panelWidthPx_, panelHeightPx_), s.used);
    return ui::rim::usedSlotByOrdinal(s, ordinal);
}

void LightScreenController::onEnter(uint32_t /*nowMs*/) {
    touchDown_ = false;
    dragged_ = false;
    dial_ = 0;
    if (view_ != nullptr) {
        view_->build(nullptr, panelWidthPx_, panelHeightPx_);
        view_->setVisible(true);
    }
}

void LightScreenController::onExit(uint32_t /*nowMs*/) {
    touchDown_ = false;
    dragged_ = false;
    if (view_ != nullptr) {
        view_->destroy();
    }
}

bool LightScreenController::onInput(const input::InputEvent& e, uint32_t nowMs) {
    const model::LedRegistryState& reg = app_.ledRegistry().state();
    switch (e.type) {
        case InputType::TouchPress:
            touchDown_ = true;
            dragged_ = false;
            pressZone_ = e.zone;
            pressX_ = e.x;
            pressY_ = e.y;
            return true;

        case InputType::TouchMove: {
            if (!touchDown_) {
                return true;
            }
            const int dx = e.x - pressX_;
            const int dy = e.y - pressY_;
            if (!dragged_ && dx * dx + dy * dy < kDragThresholdPx * kDragThresholdPx) {
                return true;
            }
            dragged_ = true;
            if (pressZone_ == HitZone::Rim) {
                const float hue = ui::rim::hueFromAngle(ui::rim::angleDeg(e.x, e.y, panelWidthPx_, panelHeightPx_));
                post(model::LightModel::makeFloat(model::LightModel::Field::Hue, hue, nowMs));
            }
            return true;
        }

        case InputType::TouchRelease: {
            touchDown_ = false;
            if (dragged_) {
                return true;  // the drag was the gesture
            }
            if (pairingRequested()) {
                if (dial_ >= 1) {
                    post(model::LedRegistryModel::make(model::LedRegistryModel::Kind::CodeIntent, 0, dial_, nowMs));
                }
                return true;
            }
            if (pressZone_ == HitZone::Rim) {
                const int slot = stripUnderRim(pressX_, pressY_);
                if (slot >= 0 && reg.slots[slot].paired) {
                    post(model::LedRegistryModel::make(model::LedRegistryModel::Kind::Select, 0, slot, nowMs));
                }
                return true;
            }
            if (pressZone_ == HitZone::Disc) {
                post(model::LightModel::make(model::LightModel::Field::Power, app_.light().state().power ? 0 : 1, nowMs));
            }
            return true;
        }

        case InputType::TouchHoldRelease: {
            touchDown_ = false;
            if (dragged_) {
                return true;  // a drag-and-hold never backs out (CTRL-22)
            }
            if (pressZone_ == HitZone::Rim) {
                const int slot = stripUnderRim(pressX_, pressY_);
                if (slot >= 0 && !reg.slots[slot].paired) {
                    post(model::LedRegistryModel::make(model::LedRegistryModel::Kind::PairIntent, 0, slot, nowMs));
                    return true;
                }
            }
            requestScreen(model::AppScreen::Info);
            return true;
        }

        case InputType::Gesture: {
            const bool fromRim = pressZone_ == HitZone::Rim;
            touchDown_ = false;
            dragged_ = false;
            if (fromRim) {
                return true;  // the lift of a ring drag
            }
            if (e.gesture == Gesture::SwipeLeft || e.gesture == Gesture::SwipeRight) {
                post(model::LightModel::make(model::LightModel::Field::EffectStep, e.gesture == Gesture::SwipeRight ? 1 : -1, nowMs));
            }
            return true;
        }

        case InputType::KnobDelta:
            if (pairingRequested()) {
                int next = dial_ == 0 ? (e.knobDelta > 0 ? 1 : 6) : dial_ + e.knobDelta;
                if (next < 1) next = 1;
                if (next > 6) next = 6;
                dial_ = next;
            } else if (e.knobDelta != 0) {
                post(model::LightModel::make(model::LightModel::Field::BrightnessDelta, e.knobDelta, nowMs));
            }
            return true;

        default:
            return false;
    }
}

void LightScreenController::tick(uint32_t /*nowMs*/) {
    if (!pairingRequested()) {
        dial_ = 0;
    }
    if (view_ != nullptr) {
        ui::LightScreenState state;
        state.light = &app_.light().state();
        state.registry = &app_.ledRegistry().state();
        state.dial = dial_;
        view_->render(state);
    }
}

}  // namespace controllers
