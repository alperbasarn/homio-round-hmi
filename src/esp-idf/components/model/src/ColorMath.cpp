#include "model/ColorMath.h"

namespace model {

uint32_t hsvToRgb(float hue, float saturation, float value) {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    const int i = static_cast<int>(hue * 6.0f);
    const float f = hue * 6.0f - static_cast<float>(i);
    const float p = value * (1.0f - saturation);
    const float q = value * (1.0f - f * saturation);
    const float t = value * (1.0f - (1.0f - f) * saturation);

    switch (i % 6) {
        case 0: r = value; g = t; b = p; break;
        case 1: r = q; g = value; b = p; break;
        case 2: r = p; g = value; b = t; break;
        case 3: r = p; g = q; b = value; break;
        case 4: r = t; g = p; b = value; break;
        case 5: r = value; g = p; b = q; break;
        default: break;
    }

    const uint8_t red = static_cast<uint8_t>(r * 255.0f);
    const uint8_t green = static_cast<uint8_t>(g * 255.0f);
    const uint8_t blue = static_cast<uint8_t>(b * 255.0f);
    return (static_cast<uint32_t>(red) << 16) |
           (static_cast<uint32_t>(green) << 8) |
           static_cast<uint32_t>(blue);
}

uint8_t channelFromRgb(uint32_t rgb, int shift) {
    return static_cast<uint8_t>((rgb >> shift) & 0xFF);
}

uint8_t lerpChannel(uint8_t from, uint8_t to, float t) {
    return static_cast<uint8_t>(from + (to - from) * t);
}

Rgb indoorTemperatureRgb(IndoorClass c) {
    const float t = c.progress;
    switch (c.band) {
        case IndoorBand::Cold:    return Rgb{lerpChannel(95, 124, t), lerpChannel(210, 230, t), lerpChannel(140, 90, t)};
        case IndoorBand::Comfort: return Rgb{124, 230, 90};
        case IndoorBand::Warm:    return Rgb{lerpChannel(124, 255, t), lerpChannel(230, 191, t), lerpChannel(90, 74, t)};
        case IndoorBand::Hot:     return Rgb{255, lerpChannel(191, 64, t), lerpChannel(74, 80, t)};
        case IndoorBand::Invalid: break;
    }
    return kNeutralGrey;
}

Rgb outdoorTemperatureRgb(OutdoorClass c) {
    const float t = c.progress;
    switch (c.band) {
        case OutdoorBand::Freezing: return Rgb{68, 214, 255};
        case OutdoorBand::Cool:     return Rgb{lerpChannel(68, 124, t), lerpChannel(214, 230, t), lerpChannel(255, 90, t)};
        case OutdoorBand::Mild:     return Rgb{lerpChannel(124, 255, t), lerpChannel(230, 191, t), lerpChannel(90, 74, t)};
        case OutdoorBand::Hot:      return Rgb{255, lerpChannel(191, 64, t), lerpChannel(74, 80, t)};
        case OutdoorBand::Invalid:  break;
    }
    return kNeutralGrey;
}

Rgb batteryRgb(BatteryBand band) {
    switch (band) {
        case BatteryBand::Critical: return Rgb{0xFF, 0x44, 0x44};
        case BatteryBand::Low:      return Rgb{0xFF, 0xAA, 0x00};
        case BatteryBand::Medium:   return Rgb{0xFF, 0xDD, 0x44};
        case BatteryBand::Good:     return Rgb{0x7B, 0xE8, 0x5A};
        case BatteryBand::Invalid:  break;
    }
    return kNeutralGrey;
}

}  // namespace model

namespace model {

float rgbToHue(uint32_t rgb) {
    const float r = static_cast<float>((rgb >> 16) & 0xFFu) / 255.0f;
    const float g = static_cast<float>((rgb >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>(rgb & 0xFFu) / 255.0f;
    const float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const float delta = mx - mn;
    if (delta <= 0.0f) {
        return 0.0f;
    }
    float h;
    if (mx == r) {
        h = (g - b) / delta;
        if (h < 0.0f) h += 6.0f;
    } else if (mx == g) {
        h = (b - r) / delta + 2.0f;
    } else {
        h = (r - g) / delta + 4.0f;
    }
    h /= 6.0f;
    return h < 0.0f ? 0.0f : (h >= 1.0f ? 0.0f : h);
}

uint32_t rgbFromHue(float hue) {
    return hsvToRgb(hue, 1.0f, 1.0f);
}

}  // namespace model
