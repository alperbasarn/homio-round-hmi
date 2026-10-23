#include "model/PowerRules.h"

#include <cstddef>

namespace model {

namespace {

struct VoltagePoint {
    float voltage;
    float percentage;
};

constexpr VoltagePoint kSocCurve[13] = {
    {4.20f, 100.0f}, {4.11f, 90.0f}, {4.03f, 80.0f}, {3.95f, 70.0f}, {3.88f, 60.0f},
    {3.82f, 50.0f},  {3.77f, 40.0f}, {3.72f, 30.0f}, {3.67f, 20.0f}, {3.61f, 12.0f},
    {3.54f, 7.0f},   {3.45f, 3.0f},  {3.30f, 0.0f},
};
constexpr size_t kSocPoints = sizeof(kSocCurve) / sizeof(kSocCurve[0]);

}  // namespace

bool isValidCellVoltage(float volts) {
    return volts == volts && volts >= 0.0f && volts < 6.0f;
}

float socPercentFromVoltage(float volts) {
    if (!isValidCellVoltage(volts)) {
        return -1.0f;
    }
    if (volts >= kSocCurve[0].voltage) {
        return 100.0f;
    }
    if (volts <= kSocCurve[kSocPoints - 1].voltage) {
        return 0.0f;
    }
    for (size_t i = 0; i + 1 < kSocPoints; ++i) {
        const VoltagePoint& high = kSocCurve[i];
        const VoltagePoint& low = kSocCurve[i + 1];
        if (volts <= high.voltage && volts >= low.voltage) {
            const float spanV = high.voltage - low.voltage;
            if (spanV <= 0.0001f) {
                return low.percentage;
            }
            const float t = (volts - low.voltage) / spanV;
            return low.percentage + t * (high.percentage - low.percentage);
        }
    }
    return 0.0f;
}

}  // namespace model
