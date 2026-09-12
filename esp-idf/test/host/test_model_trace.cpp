// test_model_trace (MDL-32): the off-task write counter follows the installed guard, a drain
// logs one line plus one per committed model, the formatters fit a fixed buffer and allocate
// nothing. The host build compiles the model with QNOB_MODEL_TRACE=1.
#include <cstring>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/AppModel.h"
#include "model/ModelTrace.h"
#include "model/TraceHooks.h"

using namespace model;

namespace {

struct Sink {
    static constexpr int kMax = 32;
    char lines[kMax][trace::kLineCapacity];
    int count = 0;
    void clear() { count = 0; }
};

void collect(const char* line, void* context) {
    Sink* sink = static_cast<Sink*>(context);
    if (sink->count < Sink::kMax) {
        std::strncpy(sink->lines[sink->count], line, trace::kLineCapacity - 1);
        sink->lines[sink->count][trace::kLineCapacity - 1] = '\0';
        ++sink->count;
    }
}

bool guardFlag = true;
bool guard(void*) { return guardFlag; }
const char* taskName(void*) { return "DisplayTask"; }

bool startsWith(const char* line, const char* prefix) { return std::strncmp(line, prefix, std::strlen(prefix)) == 0; }

Sink g_sink;

trace::Hooks hooksWithSink() {
    trace::Hooks hooks;
    hooks.log = &collect;
    hooks.logContext = &g_sink;
    hooks.onConsumerTask = &guard;
    hooks.taskName = &taskName;
    return hooks;
}

}  // namespace

TEST_CASE(off_task_writes_are_counted_only_when_the_guard_says_off_task) {
    trace::clear();
    CHECK(!trace::enabled());
    MediaModel m;
    CHECK(m.setVolume(10));                   // tracing off: nothing counted
    CHECK_EQ(trace::writes(), 0u);
    trace::install(hooksWithSink());
    CHECK(trace::enabled());
    guardFlag = true;
    CHECK(m.setVolume(20));
    CHECK_EQ(trace::writes(), 1u);
    CHECK_EQ(trace::offTaskWrites(), 0u);
    guardFlag = false;                        // a writer on the wrong task
    CHECK(m.setVolume(30));
    CHECK_EQ(trace::offTaskWrites(), 1u);
    guardFlag = true;
    trace::resetCounters();
    CHECK_EQ(trace::offTaskWrites(), 0u);
    trace::clear();
    CHECK(!trace::enabled());
}

TEST_CASE(a_drain_logs_one_line_plus_one_per_committed_model) {
    g_sink.clear();
    guardFlag = true;
    trace::install(hooksWithSink());
    AppModel app;
    MutationQueue& q = app.mutations();
    CHECK(q.post(MediaModel::make(MediaModel::Field::Volume, 70, 0)));
    CHECK(q.post(LightModel::make(LightModel::Field::Brightness, 30, 0)));
    app.drainPendingMutations(100);
    CHECK_EQ(g_sink.count, 3);
    CHECK(startsWith(g_sink.lines[0], "drain task=DisplayTask n=2 commit=2"));
    CHECK(startsWith(g_sink.lines[1], "media tgt=pc vol=70"));
    CHECK(startsWith(g_sink.lines[2], "light b=30"));
    g_sink.clear();
    app.drainPendingMutations(200);           // nothing drained, nothing committed: silent
    CHECK_EQ(g_sink.count, 0);
    CHECK_EQ(app.lastCommitMask(), 0u);
    trace::clear();
}

TEST_CASE(formatters_fit_the_line_and_allocate_nothing) {
    NO_HEAP_SCOPE();
    char line[trace::kLineCapacity];
    MediaState media;
    media.track.title.assign("a title");
    LightState light;
    ClimateState climate;
    EnvironmentState environment;
    ConnectivityState connectivity;
    PowerState power;
    NavigationState navigation;
    KnobState knob;
    CHECK(trace::formatMedia(media, line, sizeof(line)) < static_cast<int>(sizeof(line)));
    CHECK(startsWith(line, "media "));
    CHECK(trace::formatLight(light, line, sizeof(line)) < static_cast<int>(sizeof(line)));
    CHECK(trace::formatClimate(climate, line, sizeof(line)) < static_cast<int>(sizeof(line)));
    CHECK(trace::formatEnvironment(environment, line, sizeof(line)) < static_cast<int>(sizeof(line)));
    CHECK(startsWith(line, "env ----/--/-- --:-- ---"));
    CHECK(trace::formatConnectivity(connectivity, line, sizeof(line)) < static_cast<int>(sizeof(line)));
    CHECK(trace::formatPower(power, line, sizeof(line)) < static_cast<int>(sizeof(line)));
    CHECK(trace::formatNavigation(navigation, line, sizeof(line)) < static_cast<int>(sizeof(line)));
    CHECK(startsWith(line, "nav screen=BOOT mode=MEDIA"));
    CHECK(trace::formatKnob(knob, line, sizeof(line)) < static_cast<int>(sizeof(line)));
}
