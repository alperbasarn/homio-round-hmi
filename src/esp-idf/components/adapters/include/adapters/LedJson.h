#pragma once
// LedJson (LED-03): the fixed-buffer codec for the contract documents. Flat objects with string,
// number and bool values, one array of numbers (effect_params) and one array of objects
// (controllers). No cJSON and no heap: the reader walks the text in place, every output is a
// fixed field; the writers are snprintf into the caller's buffer.
#include <cstddef>
#include <cstdint>

#include "model/LedRegistryModel.h"
#include "model/LightModel.h"
#include "model/ModelTypes.h"

namespace adapters {

struct LedInfoMsg {
    model::FixedString<model::kLedIdCapacity> id;
    model::FixedString<model::kLedNameCapacity> name;
    model::FixedString<24> modelName;
    model::FixedString<16> fw;
    model::FixedString<16> ip;
    int maxLeds = 0;
};

// Which fields a `state` document carried. The six settable ones share the LightModel::Pending
// bit values, so a pending mask encodes a `set` directly.
enum LedStateField : uint16_t {
    LedStatePower = model::LightModel::PendingPower,
    LedStateBrightness = model::LightModel::PendingBrightness,
    LedStateColor = model::LightModel::PendingColor,
    LedStateEffect = model::LightModel::PendingEffect,
    LedStateParams = model::LightModel::PendingParams,
    LedStateEffectColor = model::LightModel::PendingEffectColor,
    LedStateCount = 1u << 6,
    LedStateSrc = 1u << 7,
    LedStateSeq = 1u << 8
};

struct LedStateMsg {
    model::LedStripState state;  // HMI units; fields absent from the document keep their defaults
    uint16_t fields = 0;         // LedStateField bits that were present
    model::LedSource source = model::LedSource::Unknown;
    uint32_t seq = 0;
    bool rejected = false;
};

struct LedControllersMsg {
    static constexpr size_t kMax = 4;
    model::FixedString<model::kLedIdCapacity> ids[kMax];
    model::FixedString<model::kLedNameCapacity> names[kMax];
    size_t count = 0;
    bool listsOwnId = false;
};

bool decodeLedInfo(const char* json, LedInfoMsg& out);
bool decodeLedStatus(const char* payload, bool& online);  // `online` / `offline`, quoted or bare
bool decodeLedState(const char* json, const char* ownId, LedStateMsg& out);
bool decodeLedControllers(const char* json, const char* ownId, LedControllersMsg& out);

// Encoders return the length written (as snprintf does) or -1 when the buffer is too small or
// there is nothing to say. `fields` for a set are LightModel::Pending bits.
int encodeLedSet(char* out, size_t capacity, const model::LedStripState& s, uint16_t fields, const char* from,
                 uint32_t seq);
int encodeLedPair(char* out, size_t capacity, const char* controller, const char* label);
int encodeLedPairConfirm(char* out, size_t capacity, const char* controller, int code);
int encodeLedUnpair(char* out, size_t capacity, const char* controller);

bool parseHexColor(const char* text, uint32_t& rgb);  // "#RRGGBB", either case
model::LedSource ledSourceFromText(const char* src, const char* ownId);

}  // namespace adapters
