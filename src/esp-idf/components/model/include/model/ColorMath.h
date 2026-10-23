#pragma once
// Colour arithmetic lifted out of the views (MDL-09). Bit-exact ports: hsvToRgb from
// LightController and the temperature palette from EnvironmentInfoScreen. Views convert the
// Rgb triple to their toolkit's colour type; nothing here knows about a display.
#include <cstdint>

#include "model/Bands.h"

namespace model {

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    bool operator==(const Rgb& other) const { return r == other.r && g == other.g && b == other.b; }
    bool operator!=(const Rgb& other) const { return !(*this == other); }
    uint32_t packed() const { return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b; }
};

// hue, saturation, value in 0..1 -> 0xRRGGBB. Hue 1.0 wraps through i % 6 exactly as before.
// Call sites differ on purpose: the light screen draws its swatch with value 1.0, and the MQTT
// publish uses value = level/100 so the LED brightness is folded into the colour.
uint32_t hsvToRgb(float hue, float saturation, float value);
uint8_t channelFromRgb(uint32_t rgb, int shift);
// LED-02: the ring hue of a wire colour (0..1; a grey has none and reads 0) and back at full
// saturation and value.
float rgbToHue(uint32_t rgb);
uint32_t rgbFromHue(float hue);

// static_cast<uint8_t>(from + (to - from) * t): the old lerp, kept literally so the tests can
// compare against the old colour functions bit for bit.
uint8_t lerpChannel(uint8_t from, uint8_t to, float t);

Rgb indoorTemperatureRgb(IndoorClass c);     // Invalid -> neutral grey
Rgb outdoorTemperatureRgb(OutdoorClass c);   // Invalid -> neutral grey
Rgb batteryRgb(BatteryBand band);            // Invalid -> neutral grey

constexpr Rgb kNeutralGrey{0x6B, 0x77, 0x8A};

}  // namespace model
