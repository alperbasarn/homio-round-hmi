#pragma once
// Battery rules lifted out of BatteryHandler (PERI-03): the 13-point Li-ion state-of-charge
// curve. The handler keeps acquisition; this owns the interpretation.
namespace model {

constexpr float kCellFullVolts = 4.20f;
constexpr float kCellEmptyVolts = 3.30f;

// A plausible single-cell reading. The old -1.0f "no reading" sentinel is invalid here.
bool isValidCellVoltage(float volts);

// Bit-exact port of BatteryHandler::voltageToPercentage: clamps to 100 at or above 4.20 V and to
// 0 at or below 3.30 V, linear between the 13 knees. An invalid voltage returns -1.0f so the
// boundary code can carry the old sentinel until PowerModel replaces it with percentValid.
float socPercentFromVoltage(float volts);

}  // namespace model
