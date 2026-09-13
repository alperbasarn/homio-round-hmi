#pragma once

// PERI-10: the sound side of a touch, declared here so `input` never depends on the audio
// components. LocalAudio implements it; the InputSfxListener drives it from events.

#include "input/InputEvent.h"

namespace input {

class ITouchFeedback {
public:
    virtual ~ITouchFeedback() = default;
    virtual void onTouchPress() = 0;
    virtual void onTouchRelease() = 0;
    virtual void onSwipe(Gesture direction) = 0;
};

}  // namespace input
