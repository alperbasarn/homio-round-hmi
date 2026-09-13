// INPT-06: the event record stays small and its names round-trip.
#include "input/InputEvent.h"

#include "TestRunner.h"

#include <cstring>
#include <type_traits>

using input::Gesture;
using input::InputEvent;
using input::InputType;

TEST_CASE(input_event_is_small_and_trivial) {
    static_assert(sizeof(InputEvent) < 16, "sizeof(InputEvent) must stay below 16 bytes");
    static_assert(std::is_trivially_copyable<InputEvent>::value, "InputEvent must be trivially copyable");
    InputEvent e;
    CHECK(e.type == InputType::None);
    CHECK(e.gesture == Gesture::None);
    CHECK(e.x == 0 && e.y == 0 && e.durationMs == 0 && e.knobDelta == 0);
}

TEST_CASE(input_event_names_are_distinct) {
    const InputType types[] = {InputType::None, InputType::TouchPress, InputType::TouchMove, InputType::TouchRelease,
                               InputType::TouchHoldRelease, InputType::Gesture, InputType::KnobDelta, InputType::KnobLink};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        for (size_t j = i + 1; j < sizeof(types) / sizeof(types[0]); ++j) {
            CHECK(std::strcmp(input::toString(types[i]), input::toString(types[j])) != 0);
        }
        CHECK(std::strcmp(input::toString(types[i]), "?") != 0);
    }
    CHECK(std::strcmp(input::toString(Gesture::SwipeUp), "swipe-up") == 0);
    CHECK(std::strcmp(input::toString(Gesture::LongPress), "long-press") == 0);
}

