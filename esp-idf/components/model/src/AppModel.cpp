#include "model/AppModel.h"

namespace model {

AppModel::AppModel()
    : mutations_(), clock_(), wired_(false), unrouted_(0), rejected_(0), lastCommitCount_(0),
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
    return drained;
}

size_t AppModel::commitAll() {
    size_t notified = 0;
    notified += media_.commit() ? 1 : 0;
    notified += light_.commit() ? 1 : 0;
    notified += climate_.commit() ? 1 : 0;
    notified += environment_.commit() ? 1 : 0;
    notified += connectivity_.commit() ? 1 : 0;
    notified += power_.commit() ? 1 : 0;
    notified += navigation_.commit() ? 1 : 0;
    notified += knob_.commit() ? 1 : 0;
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

}  // namespace model
