#pragma once
// The screens the router can show (MDL-26, D8) and the knob modes on the wheel.
//
// SLEEP is deliberately absent: it is a power state, not a screen. The router holds the last
// real screen while the panel is off and wake repaints it. The old global `enum Mode` (with
// INITIALIZATION, SOUND, HOME, SLEEP and the never-implemented CALIBRATE_ORIENTATION) is
// retired by DISP-08.
#include <cstdint>

namespace model {

enum class AppScreen : uint8_t {
    Boot,        // the splash ramp (InitializationScreen today)
    Info,        // time, weather, temperatures
    DeviceInfo,  // WiFi, internet, MQTT, battery
    ModeWheel,   // LIGHT / CLIMATE / MEDIA selection (ModeController today)
    Media,       // Spotify or PC, with SEEK as a state and the target overlay inside it
    Light,
    Climate,
    Count
};

enum class KnobMode : uint8_t {
    Light = 0,
    Climate = 1,
    Media = 2,
    Count = 3
};

const char* appScreenName(AppScreen screen);
const char* knobModeName(KnobMode mode);

}  // namespace model
