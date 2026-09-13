#pragma once

// INPT-13: the sound effects follow the events. Click at finger-down, release sound at finger-up
// (the legacy block played the press effect on the release and nothing on the press), one
// swipe effect per swipe.

#include "input/ITouchFeedback.h"
#include "input/InputEvent.h"

namespace input {

class InputSfxListener {
public:
    explicit InputSfxListener(ITouchFeedback* feedback = nullptr) : feedback_(feedback) {}
    void setFeedback(ITouchFeedback* feedback) { feedback_ = feedback; }

    // Returns true when the event produced a sound request.
    bool onEvent(const InputEvent& event);

private:
    ITouchFeedback* feedback_;
};

}  // namespace input
