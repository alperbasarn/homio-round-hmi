#include "model/AppModel.h"

#if QNOB_MODEL_TRACE
#include <cstdio>

#include "model/ModelTrace.h"
#endif

namespace model {

AppModel::AppModel()
    : mutations_(), clock_(), wired_(false), unrouted_(0), rejected_(0), lastCommitCount_(0), lastCommitMask_(0),
      media_(), light_(), climate_(), environment_(), connectivity_(), power_(), navigation_(), knob_() {}

bool AppModel::wireSubscriptions(WireFn wire, void* context) {
    if (wired_) {
        return false;
    }
    if (wire != nullptr) {
        wire(*this, context);
    }
    wired_ = true;
    media_.seal();
    light_.seal();
    climate_.seal();
    environment_.seal();
    connectivity_.seal();
    power_.seal();
    navigation_.seal();
    knob_.seal();
    return true;
}

size_t AppModel::drainPendingMutations(Millis nowMs) {
    const size_t drained = mutations_.drain([this, nowMs](const ModelUpdate& record) { apply(record, nowMs); });
    knob_.tick(nowMs);
    lastCommitCount_ = commitAll();
    traceDrain(drained);
    return drained;
}

size_t AppModel::commitAll() {
    uint8_t mask = 0;
    mask |= media_.commit() ? (1u << static_cast<unsigned>(Aggregate::Media)) : 0u;
    mask |= light_.commit() ? (1u << static_cast<unsigned>(Aggregate::Light)) : 0u;
    mask |= climate_.commit() ? (1u << static_cast<unsigned>(Aggregate::Climate)) : 0u;
    mask |= environment_.commit() ? (1u << static_cast<unsigned>(Aggregate::Environment)) : 0u;
    mask |= connectivity_.commit() ? (1u << static_cast<unsigned>(Aggregate::Connectivity)) : 0u;
    mask |= power_.commit() ? (1u << static_cast<unsigned>(Aggregate::Power)) : 0u;
    mask |= navigation_.commit() ? (1u << static_cast<unsigned>(Aggregate::Navigation)) : 0u;
    mask |= knob_.commit() ? (1u << static_cast<unsigned>(Aggregate::Knob)) : 0u;
    lastCommitMask_ = mask;
    size_t notified = 0;
    for (unsigned bit = 0; bit < static_cast<unsigned>(Aggregate::Count); ++bit) {
        notified += (mask >> bit) & 1u;
    }
    return notified;
}

void AppModel::apply(const ModelUpdate& record, Millis nowMs) {
    bool accepted = false;
    switch (static_cast<Aggregate>(record.aggregate)) {
        case Aggregate::Media: accepted = media_.applyUpdate(record, nowMs); break;
        case Aggregate::Light: accepted = light_.applyUpdate(record, nowMs); break;
        case Aggregate::Climate: accepted = climate_.applyUpdate(record, nowMs); break;
        case Aggregate::Environment: accepted = environment_.applyUpdate(record, nowMs); break;
        case Aggregate::Connectivity: accepted = connectivity_.applyUpdate(record, nowMs); break;
        case Aggregate::Power: accepted = power_.applyUpdate(record, nowMs); break;
        case Aggregate::Navigation: accepted = navigation_.applyUpdate(record, nowMs); break;
        case Aggregate::Knob: accepted = knob_.applyUpdate(record, nowMs); break;
        case Aggregate::Count:
        default:
            ++unrouted_;
            return;
    }
    if (!accepted) {
        ++rejected_;
    }
}

#if QNOB_MODEL_TRACE
void AppModel::traceDrain(size_t drained) const {
    if (!trace::enabled() || (drained == 0 && lastCommitCount_ == 0)) {
        return;
    }
    char line[trace::kLineCapacity];
    std::snprintf(line, sizeof(line), "drain task=%s n=%u commit=%u unrouted=%lu rejected=%lu offtask=%lu",
                  trace::currentTaskName(), static_cast<unsigned>(drained), static_cast<unsigned>(lastCommitCount_),
                  static_cast<unsigned long>(unrouted_), static_cast<unsigned long>(rejected_),
                  static_cast<unsigned long>(trace::offTaskWrites()));
    trace::log(line);
    const uint8_t mask = lastCommitMask_;
    if (mask & (1u << static_cast<unsigned>(Aggregate::Media))) { trace::formatMedia(media_.state(), line, sizeof(line)); trace::log(line); }
    if (mask & (1u << static_cast<unsigned>(Aggregate::Light))) { trace::formatLight(light_.state(), line, sizeof(line)); trace::log(line); }
    if (mask & (1u << static_cast<unsigned>(Aggregate::Climate))) { trace::formatClimate(climate_.state(), line, sizeof(line)); trace::log(line); }
    if (mask & (1u << static_cast<unsigned>(Aggregate::Environment))) { trace::formatEnvironment(environment_.state(), line, sizeof(line)); trace::log(line); }
    if (mask & (1u << static_cast<unsigned>(Aggregate::Connectivity))) { trace::formatConnectivity(connectivity_.state(), line, sizeof(line)); trace::log(line); }
    if (mask & (1u << static_cast<unsigned>(Aggregate::Power))) { trace::formatPower(power_.state(), line, sizeof(line)); trace::log(line); }
    if (mask & (1u << static_cast<unsigned>(Aggregate::Navigation))) { trace::formatNavigation(navigation_.state(), line, sizeof(line)); trace::log(line); }
    if (mask & (1u << static_cast<unsigned>(Aggregate::Knob))) { trace::formatKnob(knob_.state(), line, sizeof(line)); trace::log(line); }
}
#else
void AppModel::traceDrain(size_t drained) const {
    (void)drained;
}
#endif

}  // namespace model
