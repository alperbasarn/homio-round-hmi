// test_domain_helpers (MDL-11): ColorMath against a 1/360-step hue reference and both call
// sites' value arguments; DateFormat well-formed and every malformed shape; zero allocations.
#include <cstring>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/ColorMath.h"
#include "model/DateFormat.h"

using namespace model;

namespace {

// The old LightController::hsvToRgb, verbatim, as the oracle.
uint32_t legacyHsvToRgb(float hue, float saturation, float value) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
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
    return (static_cast<uint32_t>(static_cast<uint8_t>(r * 255.0f)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(g * 255.0f)) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(b * 255.0f));
}

bool dateIs(const char* input, const char* expected) {
    DateText out;
    formatDate(input, out);
    return std::strcmp(out.c_str(), expected) == 0;
}

}  // namespace

TEST_CASE(hsv_matches_the_old_function_over_a_full_hue_table) {
    int mismatches = 0;
    for (int step = 0; step <= 360; ++step) {
        const float hue = static_cast<float>(step) / 360.0f;
        // The light screen calls it with saturation 1 and value = level/100 (both call sites),
        // so sweep the values the drag can produce too.
        for (int level = 0; level <= 100; level += 5) {
            const float value = static_cast<float>(level) / 100.0f;
            if (hsvToRgb(hue, 1.0f, value) != legacyHsvToRgb(hue, 1.0f, value)) ++mismatches;
        }
    }
    CHECK_EQ(mismatches, 0);
    CHECK_EQ(hsvToRgb(1.0f, 1.0f, 1.0f), legacyHsvToRgb(1.0f, 1.0f, 1.0f));  // i == 6 -> i % 6 == 0
    CHECK_EQ(hsvToRgb(0.0f, 1.0f, 1.0f), 0xFF0000u);
    CHECK_EQ(channelFromRgb(0x123456u, 16), 0x12);
    CHECK_EQ(channelFromRgb(0x123456u, 8), 0x34);
    CHECK_EQ(channelFromRgb(0x123456u, 0), 0x56);
    CHECK_EQ(lerpChannel(95, 124, 0.5f), static_cast<uint8_t>(95 + (124 - 95) * 0.5f));
}

TEST_CASE(well_formed_dates) {
    CHECK(dateIs("2026/02/07", "7 FEB"));
    CHECK(dateIs("2026/12/31", "31 DEC"));
    CHECK(dateIs("2026/01/01", "1 JAN"));
    CHECK(dateIs("26/3/9", "9 MAR"));
    CHECK(dateIs("2026/10/00", "0 OCT"));  // leading zeros stripped down to one digit
    DateText out;
    CHECK(formatDate("2026/02/07", out));
}

TEST_CASE(malformed_dates_give_the_placeholder) {
    CHECK(dateIs("garbage", "-- ---"));
    CHECK(dateIs("", "-- ---"));
    CHECK(dateIs(nullptr, "-- ---"));
    CHECK(dateIs("2026/02", "-- ---"));          // one slash
    CHECK(dateIs("2026//07", "-- ---"));         // empty month
    CHECK(dateIs("2026/02/", "-- ---"));         // empty day
    CHECK(dateIs("2026/0x/07", "-- ---"));       // non-digit month
    CHECK(dateIs("2026/02/7a", "-- ---"));       // non-digit day
    DateText out;
    CHECK(!formatDate("garbage", out));
}

TEST_CASE(out_of_range_months_keep_the_day_as_before) {
    CHECK(dateIs("2026/00/07", "7 ---"));
    CHECK(dateIs("2026/13/07", "7 ---"));
    CHECK(dateIs("2026/999999999999/07", "7 ---"));  // the old std::stoi would have thrown here
    CHECK(std::strcmp(monthName(0), "---") == 0);
    CHECK(std::strcmp(monthName(13), "---") == 0);
    CHECK(std::strcmp(monthName(6), "JUN") == 0);
}

TEST_CASE(long_days_truncate_instead_of_overflowing) {
    DateText out;
    formatDate("2026/02/1234567890", out);
    CHECK_EQ(out.size(), DateText::kCapacity);
    CHECK(std::strncmp(out.c_str(), "12345678", 8) == 0);
}

TEST_CASE(colour_and_date_helpers_allocate_nothing) {
    NO_HEAP_SCOPE();
    uint32_t acc = 0;
    DateText out;
    for (int i = 0; i < 2000; ++i) {
        acc ^= hsvToRgb(static_cast<float>(i % 100) / 100.0f, 1.0f, 0.5f);
        formatDate("2026/02/07", out);
        formatDate("bad", out);
    }
    CHECK(acc != 0xFFFFFFFFu);
}
