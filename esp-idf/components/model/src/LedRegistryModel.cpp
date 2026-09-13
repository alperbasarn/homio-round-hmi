#include "model/LedRegistryModel.h"

#include <cstring>

#include "model/LightModel.h"  // the single wire-scale rule (NET-22)

namespace model {

namespace {

const char* const kEffectNames[static_cast<size_t>(LedEffect::Count)] = {
    "solid", "glow", "rainbow", "chase", "sparkle", "wave", "fire", "aurora"};
const char* const kSourceNames[static_cast<size_t>(LedSource::Count)] = {
    "unknown", "self", "controller", "web", "matter", "schedule", "device"};
const char* const kPairingNames[static_cast<size_t>(LedPairing::Count)] = {
    "idle", "requested", "confirming", "paired", "failed"};

}  // namespace

const char* ledEffectName(LedEffect effect) {
    const size_t i = static_cast<size_t>(effect);
    return i < static_cast<size_t>(LedEffect::Count) ? kEffectNames[i] : "?";
}

bool ledEffectFromName(const char* name, LedEffect& out) {
    if (name == nullptr) {
        return false;
    }
    for (size_t i = 0; i < static_cast<size_t>(LedEffect::Count); ++i) {
        if (std::strcmp(name, kEffectNames[i]) == 0) {
            out = static_cast<LedEffect>(i);
            return true;
        }
    }
    return false;
}

LedEffect stepLedEffect(LedEffect effect, int steps) {
    const int n = static_cast<int>(LedEffect::Count);
    int i = static_cast<int>(effect);
    if (i < 0 || i >= n) {
        i = 0;
    }
    i = ((i + steps) % n + n) % n;
    return static_cast<LedEffect>(i);
}

const char* ledSourceName(LedSource source) {
    const size_t i = static_cast<size_t>(source);
    return i < static_cast<size_t>(LedSource::Count) ? kSourceNames[i] : "?";
}

const char* ledPairingName(LedPairing state) {
    const size_t i = static_cast<size_t>(state);
    return i < static_cast<size_t>(LedPairing::Count) ? kPairingNames[i] : "?";
}

bool LedStripState::operator==(const LedStripState& other) const {
    return power == other.power && brightness == other.brightness && rgb == other.rgb && effect == other.effect &&
           std::memcmp(params, other.params, sizeof(params)) == 0 && effectRgb == other.effectRgb &&
           count == other.count;
}

bool LedSlot::operator==(const LedSlot& other) const {
    return id == other.id && name == other.name && used == other.used && paired == other.paired &&
           online == other.online && stateKnown == other.stateKnown && lastSeenMs == other.lastSeenMs &&
           state == other.state && lastSource == other.lastSource && lastSeq == other.lastSeq &&
           stateGeneration == other.stateGeneration;
}

bool LedRegistryState::operator==(const LedRegistryState& other) const {
    for (size_t i = 0; i < kSlots; ++i) {
        if (slots[i] != other.slots[i]) {
            return false;
        }
    }
    return selected == other.selected && used == other.used && refusedAnnounces == other.refusedAnnounces &&
           pairing == other.pairing && pairingSlot == other.pairingSlot && pairIntent == other.pairIntent &&
           unpairIntent == other.unpairIntent && codeIntent == other.codeIntent;
}

uint8_t LedRegistryModel::fieldId(Kind kind, int slot) {
    const uint8_t s = (slot < 0 || slot >= static_cast<int>(kSlots)) ? 0u : static_cast<uint8_t>(slot);
    return static_cast<uint8_t>((static_cast<uint8_t>(kind) << 3) | s);
}

RecordClass LedRegistryModel::classOf(Kind kind) {
    switch (kind) {
        case Kind::Status:
        case Kind::Paired:
        case Kind::Select:
        case Kind::Forget:
        case Kind::PairIntent:
        case Kind::CodeIntent:
        case Kind::UnpairIntent:
            return RecordClass::NeverDrop;
        default:
            return RecordClass::CoalesceLatest;
    }
}

ModelUpdate LedRegistryModel::make(Kind kind, int slot, int32_t value, uint32_t atMs, int32_t aux) {
    return ModelUpdate::ofInt(Aggregate::LedRegistry, fieldId(kind, slot), classOf(kind), value, atMs, aux);
}

bool LedRegistryModel::applyUpdate(const ModelUpdate& update, Millis now) {
    const Kind kind = kindOf(update.field);
    const int index = slotOf(update.field);
    switch (kind) {
        case Kind::Status:
            return setOnline(index, update.asBool(), now);
        case Kind::Seen:
            return noteSeen(index, now);
        case Kind::Power:
            return setPower(index, update.asBool(), now);
        case Kind::BrightnessWire:
            return setBrightnessFromWire(index, update.value, now);
        case Kind::Color:
            return setColor(index, static_cast<uint32_t>(update.value), now);
        case Kind::Effect:
            return update.value >= 0 && update.value < static_cast<int32_t>(LedEffect::Count) &&
                   setEffect(index, static_cast<LedEffect>(update.value), now);
        case Kind::ParamsLow: {
            if (!validSlot(index)) {
                return false;
            }
            uint8_t p[kLedParamCount];
            std::memcpy(p, state().slots[index].state.params, sizeof(p));
            const uint32_t v = static_cast<uint32_t>(update.value);
            p[0] = static_cast<uint8_t>(v & 0xFFu);
            p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
            p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
            p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
            return setParams(index, p, now);
        }
        case Kind::ParamsHigh: {
            if (!validSlot(index)) {
                return false;
            }
            uint8_t p[kLedParamCount];
            std::memcpy(p, state().slots[index].state.params, sizeof(p));
            p[4] = static_cast<uint8_t>(static_cast<uint32_t>(update.value) & 0xFFu);
            setParams(index, p, now);
            setCount(index, update.aux, now);
            return true;
        }
        case Kind::EffectColor:
            return setEffectColor(index, static_cast<uint32_t>(update.value), now);
        case Kind::Source:
            return update.value >= 0 && update.value < static_cast<int32_t>(LedSource::Count) &&
                   setSource(index, static_cast<LedSource>(update.value), static_cast<uint32_t>(update.aux));
        case Kind::Paired:
            return setPaired(index, update.asBool());
        case Kind::Select:
            return select(update.value);
        case Kind::Forget:
            return forget(index);
        case Kind::PairIntent:
            return requestPair(update.value);
        case Kind::CodeIntent:
            return requestCode(update.value);
        case Kind::UnpairIntent:
            return requestUnpair(update.value);
        case Kind::Count:
        default:
            return false;
    }
}

int LedRegistryModel::find(const char* id) const {
    if (id == nullptr || id[0] == '\0') {
        return -1;
    }
    for (size_t i = 0; i < kSlots; ++i) {
        const LedSlot& s = state().slots[i];
        if (s.used && std::strcmp(s.id.c_str(), id) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int LedRegistryModel::announce(const char* id, const char* name, Millis now) {
    if (id == nullptr || id[0] == '\0' || std::strlen(id) > kLedIdCapacity) {
        return -1;  // not an id this registry can hold; nothing to refuse
    }
    const int known = find(id);
    if (known >= 0) {
        LedSlot& s = mutate().slots[known];
        if (name != nullptr && name[0] != '\0' && std::strcmp(s.name.c_str(), name) != 0) {
            s.name.assign(name);
        }
        s.lastSeenMs = now;
        return known;
    }
    for (size_t i = 0; i < kSlots; ++i) {
        if (!state().slots[i].used) {
            LedRegistryState& st = mutate();
            LedSlot& s = st.slots[i];
            s = LedSlot{};
            s.id.assign(id);
            s.name.assign(name != nullptr ? name : "");
            s.used = true;
            s.lastSeenMs = now;
            ++st.used;
            return static_cast<int>(i);
        }
    }
    ++mutate().refusedAnnounces;  // the ninth strip: the bridge logs it once
    return -1;
}

bool LedRegistryModel::touch(int index, Millis now) {
    if (!validSlot(index)) {
        return false;
    }
    if (state().slots[index].lastSeenMs != now) {
        mutate().slots[index].lastSeenMs = now;
    }
    return true;
}

bool LedRegistryModel::setOnline(int index, bool online, Millis now) {
    if (!validSlot(index)) {
        return false;
    }
    LedSlot& s = mutate().slots[index];
    s.online = online;
    s.lastSeenMs = now;
    return true;
}

bool LedRegistryModel::noteSeen(int index, Millis now) {
    return touch(index, now);
}

bool LedRegistryModel::applyState(int index, const LedStripState& s, LedSource source, uint32_t seq, Millis now) {
    if (!validSlot(index)) {
        return false;
    }
    if (s.brightness > kPercentMax || s.effect >= LedEffect::Count || source >= LedSource::Count) {
        return false;
    }
    LedSlot& slot = mutate().slots[index];
    slot.state = s;
    slot.state.rgb &= 0xFFFFFFu;
    slot.state.effectRgb &= 0xFFFFFFu;
    slot.stateKnown = true;
    slot.lastSource = source;
    slot.lastSeq = seq;
    slot.lastSeenMs = now;
    ++slot.stateGeneration;
    return true;
}

bool LedRegistryModel::setPower(int index, bool power, Millis now) {
    if (!touch(index, now)) {
        return false;
    }
    LedSlot& slot = mutate().slots[index];
    slot.state.power = power;
    slot.stateKnown = true;
    ++slot.stateGeneration;
    return true;
}

bool LedRegistryModel::setBrightnessFromWire(int index, int wire, Millis now) {
    if (wire < 0 || wire > LightModel::kWireMax || !touch(index, now)) {
        return false;
    }
    LedSlot& slot = mutate().slots[index];
    slot.state.brightness = static_cast<Percent>(LightModel::percentFromWire(wire));
    slot.stateKnown = true;
    ++slot.stateGeneration;
    return true;
}

bool LedRegistryModel::setColor(int index, uint32_t rgb, Millis now) {
    if (!touch(index, now)) {
        return false;
    }
    LedSlot& slot = mutate().slots[index];
    slot.state.rgb = rgb & 0xFFFFFFu;
    slot.stateKnown = true;
    ++slot.stateGeneration;
    return true;
}

bool LedRegistryModel::setEffect(int index, LedEffect effect, Millis now) {
    if (effect >= LedEffect::Count || !touch(index, now)) {
        return false;
    }
    LedSlot& slot = mutate().slots[index];
    slot.state.effect = effect;
    slot.stateKnown = true;
    ++slot.stateGeneration;
    return true;
}

bool LedRegistryModel::setParams(int index, const uint8_t params[kLedParamCount], Millis now) {
    if (params == nullptr || !touch(index, now)) {
        return false;
    }
    LedSlot& slot = mutate().slots[index];
    std::memcpy(slot.state.params, params, kLedParamCount);
    slot.stateKnown = true;
    ++slot.stateGeneration;
    return true;
}

bool LedRegistryModel::setEffectColor(int index, uint32_t rgb, Millis now) {
    if (!touch(index, now)) {
        return false;
    }
    LedSlot& slot = mutate().slots[index];
    slot.state.effectRgb = rgb & 0xFFFFFFu;
    slot.stateKnown = true;
    ++slot.stateGeneration;
    return true;
}

bool LedRegistryModel::setCount(int index, int count, Millis now) {
    if (count < 0 || count > 65535 || !touch(index, now)) {
        return false;
    }
    mutate().slots[index].state.count = static_cast<uint16_t>(count);
    return true;
}

bool LedRegistryModel::setSource(int index, LedSource source, uint32_t seq) {
    if (source >= LedSource::Count || !validSlot(index)) {
        return false;
    }
    LedSlot& slot = mutate().slots[index];
    slot.lastSource = source;
    slot.lastSeq = seq;
    return true;
}

bool LedRegistryModel::setPaired(int index, bool paired) {
    if (!validSlot(index)) {
        return false;
    }
    if (state().slots[index].paired != paired) {
        mutate().slots[index].paired = paired;
    }
    return true;
}

bool LedRegistryModel::select(int index) {
    if (index < 0) {
        if (state().selected != -1) {
            mutate().selected = -1;
        }
        return true;
    }
    if (!validSlot(index)) {
        return false;
    }
    if (state().selected != index) {
        mutate().selected = static_cast<int8_t>(index);
    }
    return true;
}

bool LedRegistryModel::selectById(const char* id) {
    const int index = find(id);
    return index >= 0 && select(index);
}

bool LedRegistryModel::forget(int index) {
    if (!validSlot(index)) {
        return false;
    }
    LedRegistryState& st = mutate();
    st.slots[index] = LedSlot{};
    --st.used;
    if (st.selected == index) {
        st.selected = -1;
    }
    return true;
}

void LedRegistryModel::tick(Millis now) {
    for (size_t i = 0; i < kSlots; ++i) {
        const LedSlot& s = state().slots[i];
        if (s.used && s.online && now - s.lastSeenMs > kPresenceTimeoutMs) {
            mutate().slots[i].online = false;
        }
    }
}

size_t LedRegistryModel::pairedCount() const {
    size_t n = 0;
    for (size_t i = 0; i < kSlots; ++i) {
        n += (state().slots[i].used && state().slots[i].paired) ? 1u : 0u;
    }
    return n;
}

size_t LedRegistryModel::onlineCount() const {
    size_t n = 0;
    for (size_t i = 0; i < kSlots; ++i) {
        n += (state().slots[i].used && state().slots[i].online) ? 1u : 0u;
    }
    return n;
}

bool LedRegistryModel::commandsAllowed(int index) const {
    const LedSlot* s = slot(index);
    return s != nullptr && s->online && s->paired;
}

bool LedRegistryModel::requestPair(int index) {
    if (!validSlot(index)) {
        return false;
    }
    mutate().pairIntent = static_cast<int8_t>(index);
    return true;
}

bool LedRegistryModel::requestCode(int code) {
    if (code < 1 || code > 6) {
        return false;
    }
    mutate().codeIntent = static_cast<uint8_t>(code);
    return true;
}

bool LedRegistryModel::requestUnpair(int index) {
    if (!validSlot(index)) {
        return false;
    }
    mutate().unpairIntent = static_cast<int8_t>(index);
    return true;
}

int LedRegistryModel::takePairIntent() {
    const int out = state().pairIntent;
    if (out >= 0) {
        mutate().pairIntent = -1;
    }
    return out;
}

int LedRegistryModel::takeCodeIntent() {
    const int out = state().codeIntent;
    if (out != 0) {
        mutate().codeIntent = 0;
    }
    return out;
}

int LedRegistryModel::takeUnpairIntent() {
    const int out = state().unpairIntent;
    if (out >= 0) {
        mutate().unpairIntent = -1;
    }
    return out;
}

bool LedRegistryModel::setPairing(LedPairing pairing, int index) {
    if (pairing >= LedPairing::Count) {
        return false;
    }
    const int8_t slot = (index >= 0 && index < static_cast<int>(kSlots)) ? static_cast<int8_t>(index) : int8_t{-1};
    if (state().pairing != pairing || state().pairingSlot != slot) {
        LedRegistryState& s = mutate();
        s.pairing = pairing;
        s.pairingSlot = slot;
    }
    return true;
}

}  // namespace model
