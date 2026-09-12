#pragma once
// ModelTrace, the formatting half (MDL-32): one fixed-buffer line per model state, so a knob
// walk or an INFO refresh can be read back from the log and compared with the panel. Nothing
// here allocates; every formatter returns the number of characters it wanted, like snprintf.
#include <cstddef>

#include "model/ClimateModel.h"
#include "model/ConnectivityModel.h"
#include "model/EnvironmentModel.h"
#include "model/KnobModel.h"
#include "model/LightModel.h"
#include "model/MediaModel.h"
#include "model/NavigationModel.h"
#include "model/PowerModel.h"
#include "model/TraceHooks.h"

namespace model {
namespace trace {

constexpr size_t kLineCapacity = 192;

int formatMedia(const MediaState& s, char* out, size_t capacity);
int formatLight(const LightState& s, char* out, size_t capacity);
int formatClimate(const ClimateState& s, char* out, size_t capacity);
int formatEnvironment(const EnvironmentState& s, char* out, size_t capacity);
int formatConnectivity(const ConnectivityState& s, char* out, size_t capacity);
int formatPower(const PowerState& s, char* out, size_t capacity);
int formatNavigation(const NavigationState& s, char* out, size_t capacity);
int formatKnob(const KnobState& s, char* out, size_t capacity);

}  // namespace trace
}  // namespace model
