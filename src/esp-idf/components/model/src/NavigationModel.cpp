#include "model/NavigationModel.h"

namespace model {

const char* appScreenName(AppScreen screen) {
    switch (screen) {
        case AppScreen::Boot: return "BOOT";
        case AppScreen::Info: return "INFO";
        case AppScreen::DeviceInfo: return "DEVICE_INFO";
        case AppScreen::ModeWheel: return "MODE_WHEEL";
        case AppScreen::Media: return "MEDIA";
        case AppScreen::Light: return "LIGHT";
        case AppScreen::Climate: return "CLIMATE";
        case AppScreen::Count: break;
    }
    return "?";
}

const char* knobModeName(KnobMode mode) {
    switch (mode) {
        case KnobMode::Light: return "LIGHT";
        case KnobMode::Climate: return "CLIMATE";
        case KnobMode::Media: return "MEDIA";
        case KnobMode::Count: break;
    }
    return "?";
}

RecordClass NavigationModel::classOf(Field field) {
    switch (field) {
        case Field::Activity:
        case Field::InitProgress:
            return RecordClass::CoalesceLatest;
        case Field::Request:
        case Field::Mode:
        case Field::ModeStep:
        case Field::ActiveScreen:
        case Field::Count:
        default:
            return RecordClass::NeverDrop;
    }
}

ModelUpdate NavigationModel::make(Field field, int32_t value, uint32_t atMs, int32_t aux) {
    return ModelUpdate::ofInt(Aggregate::Navigation, static_cast<uint8_t>(field), classOf(field), value, atMs, aux);
}

bool NavigationModel::applyUpdate(const ModelUpdate& update, Millis now) {
    switch (static_cast<Field>(update.field)) {
        case Field::Request:
            return requestScreen(static_cast<AppScreen>(update.value), static_cast<TransitionType>(update.aux));
        case Field::Activity:
            noteActivity(now);
            return true;
        case Field::InitProgress:
            return setInitProgress(update.value);
        case Field::Mode:
            return selectMode(static_cast<KnobMode>(update.value));
        case Field::ModeStep:
            stepMode(update.value);
            return true;
        case Field::ActiveScreen:
            return setActiveScreen(static_cast<AppScreen>(update.value));
        case Field::Count:
        default:
            return false;
    }
}

bool NavigationModel::canTransition(AppScreen from, AppScreen to) {
    if (from >= AppScreen::Count || to >= AppScreen::Count || from == to) {
        return false;
    }
    switch (from) {
        case AppScreen::Boot:
            return to == AppScreen::Info;
        case AppScreen::Info:
            return to == AppScreen::ModeWheel || to == AppScreen::DeviceInfo;
        case AppScreen::DeviceInfo:
            return to == AppScreen::Info;
        case AppScreen::ModeWheel:
            return to == AppScreen::Info || to == AppScreen::Media || to == AppScreen::Light ||
                   to == AppScreen::Climate;
        case AppScreen::Media:
        case AppScreen::Light:
        case AppScreen::Climate:
            return to == AppScreen::ModeWheel || to == AppScreen::Info;  // back, or the inactivity return
        case AppScreen::Count:
            break;
    }
    return false;
}

bool NavigationModel::requestScreen(AppScreen screen, TransitionType transition) {
    if (!canTransition(state().activeScreen, screen)) {
        return false;
    }
    if (transition != TransitionType::Immediate && transition != TransitionType::Animated) {
        return false;
    }
    NavigationState& s = mutate();
    s.requestPending = true;
    s.requestedScreen = screen;
    s.requestedTransition = transition;
    return true;
}

NavigationRequest NavigationModel::takeRequest() {
    NavigationRequest request;
    const NavigationState& cur = state();
    if (!cur.requestPending) {
        return request;
    }
    request.screen = cur.requestedScreen;
    request.transition = cur.requestedTransition;
    NavigationState& s = mutate();
    s.requestPending = false;
    s.requestedScreen = AppScreen::Count;
    s.requestedTransition = TransitionType::Immediate;
    ++s.requestsConsumed;
    return request;
}

bool NavigationModel::setActiveScreen(AppScreen screen) {
    if (screen >= AppScreen::Count) {
        return false;
    }
    NavigationState& s = mutate();
    if (s.activeScreen == AppScreen::Boot && screen != AppScreen::Boot) {
        s.initProgressPct = 100;
    }
    s.activeScreen = screen;
    return true;
}

void NavigationModel::noteActivity(Millis now) {
    if (now > state().lastActivityMs) {
        mutate().lastActivityMs = now;
    }
}

Millis NavigationModel::idleMs(Millis now) const {
    const Millis last = state().lastActivityMs;
    return now > last ? now - last : 0;
}

bool NavigationModel::setInitProgress(int percent) {
    const int clamped = clampPercent(percent);
    if (clamped < state().initProgressPct) {
        return false;  // the ramp never jumps backwards
    }
    if (clamped == state().initProgressPct) {
        return true;
    }
    mutate().initProgressPct = static_cast<uint8_t>(clamped);
    return true;
}

void NavigationModel::resetInitProgress() {
    mutate().initProgressPct = 0;
}

bool NavigationModel::selectMode(KnobMode mode) {
    if (mode >= KnobMode::Count) {
        return false;
    }
    mutate().selectedMode = mode;
    return true;
}

KnobMode NavigationModel::stepMode(int steps) {
    const int count = static_cast<int>(KnobMode::Count);
    int index = (static_cast<int>(state().selectedMode) + steps) % count;
    if (index < 0) {
        index += count;
    }
    const KnobMode next = static_cast<KnobMode>(index);
    if (next != state().selectedMode) {
        mutate().selectedMode = next;
    }
    return next;
}

AppScreen NavigationModel::screenFor(KnobMode mode) {
    switch (mode) {
        case KnobMode::Light: return AppScreen::Light;
        case KnobMode::Climate: return AppScreen::Climate;
        case KnobMode::Media: return AppScreen::Media;
        case KnobMode::Count: break;
    }
    return AppScreen::Count;
}

}  // namespace model
