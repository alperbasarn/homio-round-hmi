#pragma once
// NavigationRequest (SCRN-04): the navigation outbox of a screen controller. The controller writes
// an intent; the router polls it once per frame with a destructive read, so an intent is acted on
// exactly once and a second read reports an invalid request. The payload is the model type: the
// router turns it into NavigationModel::Field::Request (never-drop), and from there the
// single-consumption rule of the model applies (MDL-26).
#include "model/NavigationModel.h"

namespace controllers {

using NavigationRequest = model::NavigationRequest;

class PendingNavigation {
public:
    // A later request before the read replaces the earlier one: only the latest intent counts.
    void request(model::AppScreen screen,
                 model::TransitionType transition = model::TransitionType::Immediate) {
        pending_.screen = screen;
        pending_.transition = transition;
    }

    bool pending() const { return pending_.valid(); }

    // Returns the intent and clears it; the next call returns an invalid request.
    NavigationRequest take() {
        const NavigationRequest out = pending_;
        pending_ = NavigationRequest{};
        return out;
    }

    void clear() { pending_ = NavigationRequest{}; }

private:
    NavigationRequest pending_{};
};

}  // namespace controllers
