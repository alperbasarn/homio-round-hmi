#include "DeviceInfoScreenController.h"

namespace controllers {

using input::InputType;
using ui::deviceinfo::contains;
using ui::deviceinfo::percentFromBarY;

DeviceInfoScreenController::DeviceInfoScreenController(model::AppModel& app, media::MediaTargetRegistry& targets,
                                                       ui::ScreenView<ui::DeviceInfoScreenState>* view,
                                                       int panelWidthPx, int panelHeightPx)
    : app_(app), targets_(targets), view_(view), layout_(ui::deviceinfo::layoutFor(panelWidthPx, panelHeightPx)) {}

void DeviceInfoScreenController::setBrightnessAccess(PercentGetter getter, PercentSetter setter, void* context) {
    brightnessGetter_ = getter;
    brightnessSetter_ = setter;
    brightnessContext_ = context;
    brightnessShadow_ = brightness();
}

int DeviceInfoScreenController::brightness() const {
    if (brightnessGetter_ == nullptr) {
        return brightnessShadow_;
    }
    const int percent = brightnessGetter_(brightnessContext_);
    return percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

void DeviceInfoScreenController::setBrightness(int percent, uint32_t /*nowMs*/) {
    if (percent == brightnessShadow_) {
        return;  // a finger resting on the bar is one event per frame; only a change is work
    }
    brightnessShadow_ = percent;
    if (brightnessSetter_ != nullptr) {
        brightnessSetter_(percent, brightnessContext_);
    }
}

void DeviceInfoScreenController::setSound(int percent, uint32_t nowMs) {
    if (percent == soundShadow_) {
        return;
    }
    soundShadow_ = percent;
    app_.mutations().post(model::MediaModel::make(model::MediaModel::Field::VolumeLocal, percent, nowMs));
    // The target coalesces the wire side (one setpoint per 120 ms) and suppresses its own echo.
    targets_.target(app_.media().state().activeTarget).setVolume(percent, nowMs);
}

void DeviceInfoScreenController::onEnter(uint32_t /*nowMs*/) {
    // A request the router never polled must not outlive the screen: `update()` consumes a
    // pending mode change before it reaches this screen's case, so a tap on the way out can leave
    // an intent behind that would fire on the next entry. The old screens cleared their request
    // flags on every entry; this is that clear.
    cancelNavigationRequest();
    pageStep_ = 0;
    brightnessShadow_ = brightness();
    soundShadow_ = static_cast<int>(app_.media().state().volume);
    if (view_ != nullptr) {
        view_->build(nullptr, layout_.panelW, layout_.panelH);
        view_->setVisible(true);
    }
}

void DeviceInfoScreenController::onExit(uint32_t /*nowMs*/) {
    cancelNavigationRequest();
    pageStep_ = 0;
    if (view_ != nullptr) {
        view_->destroy();
    }
}

int DeviceInfoScreenController::takePageStep() {
    const int step = pageStep_;
    pageStep_ = 0;
    return step;
}

bool DeviceInfoScreenController::onInput(const input::InputEvent& e, uint32_t nowMs) {
    switch (e.type) {
        case InputType::TouchPress:
        case InputType::TouchMove: {
            // The drag is absolute, as the page has always been: the fill follows the finger.
            if (contains(layout_.brightnessTouch, e.x, e.y)) {
                setBrightness(percentFromBarY(e.y, layout_.brightnessBar.y, layout_.brightnessBar.h), nowMs);
            } else if (contains(layout_.soundTouch, e.x, e.y)) {
                setSound(percentFromBarY(e.y, layout_.soundBar.y, layout_.soundBar.h), nowMs);
            }
            return true;
        }

        case InputType::TouchRelease:
        case InputType::TouchHoldRelease: {
            if (contains(layout_.bottomBand, e.x, e.y)) {
                requestScreen(model::AppScreen::Info);
            } else if (contains(layout_.leftEdge, e.x, e.y)) {
                pageStep_ = -1;
            } else if (contains(layout_.rightEdge, e.x, e.y)) {
                pageStep_ = 1;
            }
            return true;
        }

        case InputType::Gesture:
            return true;  // the page has no gestures of its own and takes no matrix swipe

        default:
            return false;  // knob events fall through to the router's default
    }
}

void DeviceInfoScreenController::tick(uint32_t /*nowMs*/) {
    if (view_ == nullptr) {
        return;
    }
    ui::DeviceInfoScreenState state;
    state.connectivity = &app_.connectivity().state();
    state.power = &app_.power().state();
    state.brightnessPercent = brightness();
    state.soundPercent = static_cast<int>(app_.media().state().volume);
    state.bluetoothConnected = false;  // no BLE in this tree
    view_->render(state);
}

}  // namespace controllers
