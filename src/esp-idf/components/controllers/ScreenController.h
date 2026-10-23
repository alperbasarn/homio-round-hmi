#pragma once
// ScreenController (SCRN-04): the base of every per-screen controller (plan, "Controller Layer").
// One controller per AppScreen. It owns input interpretation, timing, model mutation and the
// navigation requests of its screen; it owns no LVGL handle (the view does) and polls no input
// (the router delivers events).
//
// Lifecycle, driven by the router (ScreenRouter in Phase 4; DisplayController until then):
//   onEnter(now)     the screen became active; its view is built and visible
//   onInput(e, now)  every InputEvent while active; true = consumed, false = the router applies
//                    its defaults (matrix navigation, knob deltas)
//   tick(now)        once per display frame while active
//   onExit(now)      the screen is leaving; the view is hidden or destroyed afterwards
// Navigation: requestScreen() from any hook; the router reads it once per frame through
// takeNavigationRequest(), a destructive read (NavigationRequest.h).
#include <cstdint>

#include "NavigationRequest.h"
#include "input/InputEvent.h"
#include "model/AppScreen.h"

namespace controllers {

class ScreenController {
public:
    virtual ~ScreenController() = default;

    virtual model::AppScreen screen() const = 0;

    virtual void onEnter(uint32_t /*nowMs*/) {}
    virtual void onExit(uint32_t /*nowMs*/) {}
    virtual bool onInput(const input::InputEvent& /*event*/, uint32_t /*nowMs*/) { return false; }
    virtual void tick(uint32_t /*nowMs*/) {}

    bool hasNavigationRequest() const { return navigation_.pending(); }
    NavigationRequest takeNavigationRequest() { return navigation_.take(); }

protected:
    void requestScreen(model::AppScreen target,
                       model::TransitionType transition = model::TransitionType::Immediate) {
        navigation_.request(target, transition);
    }
    void cancelNavigationRequest() { navigation_.clear(); }

private:
    PendingNavigation navigation_;
};

}  // namespace controllers
