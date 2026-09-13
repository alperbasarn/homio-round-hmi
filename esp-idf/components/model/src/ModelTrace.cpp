#include "model/ModelTrace.h"

#include <cstdio>

namespace model {
namespace trace {

namespace {

const char* targetName(MediaTargetId id) {
    return id == MediaTargetId::Spotify ? "spotify" : (id == MediaTargetId::Pc ? "pc" : "?");
}

const char* bindingName(KnobBinding b) {
    return b == KnobBinding::Seek ? "seek" : "volume";
}

const char* validMark(bool valid) {
    return valid ? "" : "?";
}

}  // namespace

int formatMedia(const MediaState& s, char* out, size_t capacity) {
    return std::snprintf(out, capacity, "media tgt=%s vol=%u play=%d sync=%d bind=%s avail=%u/%u pos=%ld/%ld seek=%d",
                         targetName(s.activeTarget), static_cast<unsigned>(s.volume), s.isPlaying ? 1 : 0,
                         s.stateSynced ? 1 : 0, bindingName(s.knobBinding),
                         static_cast<unsigned>(s.availability[0]), static_cast<unsigned>(s.availability[1]),
                         static_cast<long>(s.positionMs), static_cast<long>(s.durationMs),
                         static_cast<int>(s.pendingSeekPercent));
}

int formatLight(const LightState& s, char* out, size_t capacity) {
    return std::snprintf(out, capacity, "light b=%u pwr=%d rgb=%06lx hue=%.2f fx=%s slot=%d",
                         static_cast<unsigned>(s.brightness), s.power ? 1 : 0, static_cast<unsigned long>(s.rgb),
                         static_cast<double>(s.hue), ledEffectName(s.effect), static_cast<int>(s.stripSlot));
}

int formatLedRegistry(const LedRegistryState& s, char* out, size_t capacity) {
    int n = std::snprintf(out, capacity, "leds n=%u sel=%d", static_cast<unsigned>(s.used), static_cast<int>(s.selected));
    for (size_t i = 0; i < LedRegistryState::kSlots && n >= 0 && static_cast<size_t>(n) < capacity; ++i) {
        const LedSlot& slot = s.slots[i];
        if (!slot.used) {
            continue;
        }
        n += std::snprintf(out + n, capacity - static_cast<size_t>(n), " %u:%s%s%s%s b=%u fx=%u", static_cast<unsigned>(i),
                           slot.id.c_str(), slot.online ? "+" : "-", slot.paired ? "*" : "", slot.stateKnown ? "" : "?",
                           static_cast<unsigned>(slot.state.brightness), static_cast<unsigned>(slot.state.effect));
    }
    return n;
}

int formatClimate(const ClimateState& s, char* out, size_t capacity) {
    return std::snprintf(out, capacity, "climate target=%.1f band=%u hum=%.0f%s press=%.0f%s sampled=%lld pub=%d",
                         static_cast<double>(s.targetTemp), static_cast<unsigned>(s.targetBand),
                         static_cast<double>(s.humidityPct), validMark(s.humidityValid),
                         static_cast<double>(s.pressureHpa), validMark(s.pressureValid),
                         static_cast<long long>(s.indoorSampledAtMs), s.publishPending ? 1 : 0);
}

int formatEnvironment(const EnvironmentState& s, char* out, size_t capacity) {
    return std::snprintf(out, capacity, "env %s %s %s (%s) in=%.1f%s out=%.1f%s", s.date.c_str(), s.time.c_str(),
                         s.day.c_str(), s.formatted.c_str(), static_cast<double>(s.indoorTemp),
                         validMark(s.indoorValid), static_cast<double>(s.outdoorTemp), validMark(s.outdoorValid));
}

int formatConnectivity(const ConnectivityState& s, char* out, size_t capacity) {
    return std::snprintf(out, capacity, "net wifi=%d inet=%d mqtt=%d bars=%u rssi=%d lightcfg=%d",
                         s.wifiConnected ? 1 : 0, s.internetAvailable ? 1 : 0, s.mqttConnected ? 1 : 0,
                         static_cast<unsigned>(s.bars), static_cast<int>(s.rssiDbm), s.mqttLightConfigured ? 1 : 0);
}

int formatPower(const PowerState& s, char* out, size_t capacity) {
    return std::snprintf(out, capacity, "power pct=%.0f%s v=%.2f%s conn=%d known=%d chg=%d band=%u",
                         static_cast<double>(s.percent), validMark(s.percentValid), static_cast<double>(s.voltage),
                         validMark(s.voltageValid), s.connected ? 1 : 0, s.presenceKnown ? 1 : 0,
                         s.charging ? 1 : 0, static_cast<unsigned>(s.band));
}

int formatNavigation(const NavigationState& s, char* out, size_t capacity) {
    return std::snprintf(out, capacity, "nav screen=%s mode=%s req=%s init=%u activity=%lld",
                         appScreenName(s.activeScreen), knobModeName(s.selectedMode),
                         s.requestPending ? appScreenName(s.requestedScreen) : "-",
                         static_cast<unsigned>(s.initProgressPct), static_cast<long long>(s.lastActivityMs));
}

int formatKnob(const KnobState& s, char* out, size_t capacity) {
    return std::snprintf(out, capacity, "knob count=%d ctx=%u alive=%d pend=%d acc=%d dropped=%lu clamped=%lu",
                         static_cast<int>(s.detentCount), static_cast<unsigned>(s.context), s.linkAlive ? 1 : 0,
                         static_cast<int>(s.pendingWrite), static_cast<int>(s.accumulator),
                         static_cast<unsigned long>(s.droppedBursts), static_cast<unsigned long>(s.clampedBursts));
}

}  // namespace trace
}  // namespace model
