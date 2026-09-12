// test_app_model (MDL-30): every aggregate routes, coalesced records collapse to one notify per
// model per drain, a subscribe after wiring is refused, unknown records are counted, the sizes
// are printed for the MDL-01 budget, and a drain allocates nothing.
#include <cstdio>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/AppModel.h"

using namespace model;

namespace {

void countMedia(const MediaState&, void* context) { ++*static_cast<int*>(context); }
void countLight(const LightState&, void* context) { ++*static_cast<int*>(context); }

void wireCounters(AppModel& app, void* context) {
    CHECK(app.media().subscribe(&countMedia, context));
    CHECK(app.light().subscribe(&countLight, context));
}

}  // namespace

TEST_CASE(records_route_to_every_aggregate) {
    AppModel app;
    MutationQueue& q = app.mutations();
    CHECK(q.post(MediaModel::make(MediaModel::Field::Volume, 70, 0)));
    CHECK(q.post(LightModel::make(LightModel::Field::Brightness, 30, 0)));
    CHECK(q.post(ClimateModel::makeFloat(ClimateModel::Field::Target, 22.5f, 0)));
    CHECK(q.post(EnvironmentModel::makeFloat(EnvironmentModel::Field::IndoorTemp, 21.0f, 0)));
    CHECK(q.post(ConnectivityModel::make(ConnectivityModel::Field::Wifi, 1, 0)));
    CHECK(q.post(PowerModel::makeFloat(PowerModel::Field::Percent, 80.0f, 0)));
    CHECK(q.post(NavigationModel::make(NavigationModel::Field::Request, static_cast<int32_t>(AppScreen::Info), 0,
                                       static_cast<int32_t>(TransitionType::Immediate))));
    CHECK(q.post(KnobModel::make(KnobModel::Field::Delta, 2, 0)));
    CHECK_EQ(app.drainPendingMutations(1000), 8u);
    CHECK_EQ(app.unroutedRecords(), 0u);
    CHECK_EQ(app.rejectedRecords(), 0u);
    CHECK_EQ(app.media().state().volume, 70);
    CHECK_EQ(app.light().state().brightness, 30);
    CHECK_EQ(app.climate().state().targetTemp, 22.5f);
    CHECK_EQ(app.environment().state().indoorTemp, 21.0f);
    CHECK(app.environment().state().indoorValid);
    CHECK(app.connectivity().state().wifiConnected);
    CHECK_EQ(app.power().state().percent, 80.0f);
    CHECK(app.navigation().hasRequest());
    CHECK_EQ(app.knob().state().detentCount, 2);
    CHECK_EQ(app.lastCommitCount(), 8u);
    CHECK_EQ(app.media().readSnapshot().volume, 70);  // the snapshot follows the commit
}

TEST_CASE(coalesced_records_notify_each_model_once_per_drain) {
    AppModel app;
    int notified = 0;
    CHECK(app.wireSubscriptions(&wireCounters, &notified));
    MutationQueue& q = app.mutations();
    CHECK(q.post(MediaModel::make(MediaModel::Field::Volume, 10, 0)));
    CHECK(q.post(MediaModel::make(MediaModel::Field::Volume, 20, 1)));
    CHECK(q.post(MediaModel::make(MediaModel::Field::Volume, 30, 2)));
    CHECK(q.post(LightModel::make(LightModel::Field::Brightness, 90, 2)));
    CHECK_EQ(app.drainPendingMutations(10), 2u);          // three volumes collapsed into one slot
    CHECK_EQ(app.media().state().volume, 30);
    CHECK_EQ(app.lastCommitCount(), 2u);
    CHECK_EQ(notified, 2);
    CHECK_EQ(app.drainPendingMutations(20), 0u);          // nothing pending: nobody notified
    CHECK_EQ(app.lastCommitCount(), 0u);
    CHECK_EQ(notified, 2);
}

TEST_CASE(subscribe_after_wiring_is_refused) {
    AppModel app;
    int notified = 0;
    CHECK(app.media().subscribe(&countMedia, &notified));  // before wiring: fine
    CHECK(!app.wired());
    CHECK(app.wireSubscriptions());
    CHECK(app.wired());
    CHECK(!app.media().subscribe(&countMedia, &notified));
    CHECK(!app.knob().subscribe(nullptr, nullptr));
    CHECK(!app.wireSubscriptions());
    CHECK_EQ(app.media().subscriberCount(), 1u);
}

TEST_CASE(unknown_aggregates_and_fields_are_counted_not_applied) {
    AppModel app;
    MutationQueue& q = app.mutations();
    CHECK(q.post(ModelUpdate::ofInt(Aggregate::Count, 0, RecordClass::NeverDrop, 1, 0)));
    CHECK(q.post(ModelUpdate::ofInt(Aggregate::Media, 200, RecordClass::NeverDrop, 1, 0)));
    CHECK(q.post(MediaModel::make(MediaModel::Field::Volume, 500, 0)));  // out of range
    CHECK_EQ(app.drainPendingMutations(0), 3u);
    CHECK_EQ(app.unroutedRecords(), 1u);
    CHECK_EQ(app.rejectedRecords(), 2u);
    CHECK_EQ(app.media().state().volume, 50);
}

TEST_CASE(sizes_are_printed_for_the_budget) {
    std::printf("sizeof: ModelUpdate=%zu MediaModel=%zu LightModel=%zu ClimateModel=%zu EnvironmentModel=%zu "
                "ConnectivityModel=%zu PowerModel=%zu NavigationModel=%zu KnobModel=%zu MutationQueue=%zu AppModel=%zu\n",
                sizeof(ModelUpdate), sizeof(MediaModel), sizeof(LightModel), sizeof(ClimateModel),
                sizeof(EnvironmentModel), sizeof(ConnectivityModel), sizeof(PowerModel), sizeof(NavigationModel),
                sizeof(KnobModel), sizeof(MutationQueue), sizeof(AppModel));
    // Host pointers are 8 bytes; the device figure comes from size-components once APP-06
    // instantiates the AppModel. The host ceiling only catches gross regressions.
    CHECK(sizeof(AppModel) <= 8192);
    CHECK(sizeof(MediaModel) <= 1536);
}

TEST_CASE(a_drain_allocates_nothing) {
    AppModel app;
    int notified = 0;
    CHECK(app.wireSubscriptions(&wireCounters, &notified));
    NO_HEAP_SCOPE();
    MutationQueue& q = app.mutations();
    for (int frame = 0; frame < 500; ++frame) {
        q.post(MediaModel::make(MediaModel::Field::Volume, frame % 101, frame));
        q.post(LightModel::make(LightModel::Field::Brightness, (frame * 7) % 101, frame));
        q.post(KnobModel::make(KnobModel::Field::Delta, frame % 2 ? 1 : -1, frame));
        q.post(EnvironmentModel::make(EnvironmentModel::Field::Epoch, 1770467640 + frame * 60, frame, 180));
        app.drainPendingMutations(frame * 16);
    }
    CHECK(notified > 0);
    CHECK_EQ(app.unroutedRecords(), 0u);
}
