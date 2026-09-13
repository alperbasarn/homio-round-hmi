// test_led_bridge (LED-07 at the model and codec level): a fake broker with retained topics and a
// fake strip that answers as the contract says, driven through LedBridgeCore and the aggregate
// root. announce -> available, pair -> mine (one tap when fresh, a code otherwise), set -> state
// echo suppressed and only the changed fields sent, a remote src applied, LWT -> offline and
// commands refused, retained replay on reconnect, the 8-slot cap, no allocation on the path.
#include <cstdio>
#include <cstring>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "adapters/LedBridgeCore.h"
#include "adapters/LedJson.h"
#include "adapters/LedTopics.h"
#include "model/AppModel.h"

using namespace adapters;
using namespace model;

namespace {

constexpr const char* kOwn = "qnob-0a0b0c";
constexpr const char* kStrip = "led-a1b2c3";

// The broker keeps one retained document per topic and replays them on subscribe.
struct FakeBroker {
    static constexpr size_t kMax = 32;
    struct Entry {
        char topic[64];
        char payload[256];
        bool used;
    };
    Entry retained[kMax] = {};
    void retain(const char* topic, const char* payload) {
        for (Entry& e : retained) {
            if (e.used && std::strcmp(e.topic, topic) == 0) {
                std::strncpy(e.payload, payload, sizeof(e.payload) - 1);
                return;
            }
        }
        for (Entry& e : retained) {
            if (!e.used) {
                e.used = true;
                std::strncpy(e.topic, topic, sizeof(e.topic) - 1);
                std::strncpy(e.payload, payload, sizeof(e.payload) - 1);
                return;
            }
        }
    }
    void replay(LedBridgeCore& core, Millis now) {
        for (const Entry& e : retained) {
            if (e.used) core.handleInbound(e.topic, e.payload, now);
        }
    }
};

// The strip: a tuple, a paired flag, the blink code, and the answers of the contract.
struct FakeStrip {
    LedStripState tuple;
    bool paired = false;
    bool everPaired = false;
    bool windowOpen = false;
    int code = 3;
    int publishes = 0;
    int rejected = 0;
    char lastSet[256] = {};

    void publishState(FakeBroker& b, LedBridgeCore& core, const char* src, uint32_t seq, bool reject, Millis now) {
        char topic[64];
        formatLedTopic(topic, sizeof(topic), kStrip, LedTopic::State);
        char json[256];
        std::snprintf(json, sizeof(json),
                      "{\"power\":%s,\"brightness\":%d,\"color\":\"#%06lx\",\"effect\":\"%s\",\"effect_params\":[%u,%u,%u,%u,%u],"
                      "\"effect_color\":\"#%06lx\",\"count\":60,\"src\":\"%s\",\"seq\":%lu%s}",
                      tuple.power ? "true" : "false", LightModel::wireFromPercent(tuple.brightness),
                      static_cast<unsigned long>(tuple.rgb), ledEffectName(tuple.effect), tuple.params[0], tuple.params[1],
                      tuple.params[2], tuple.params[3], tuple.params[4], static_cast<unsigned long>(tuple.effectRgb), src,
                      static_cast<unsigned long>(seq), reject ? ",\"rejected\":true" : "");
        b.retain(topic, json);
        core.handleInbound(topic, json, now);
        ++publishes;
    }
    void publishControllers(FakeBroker& b, LedBridgeCore& core, Millis now) {
        char topic[64];
        formatLedTopic(topic, sizeof(topic), kStrip, LedTopic::Controllers);
        const char* json = paired ? "{\"paired\":[{\"id\":\"qnob-0a0b0c\",\"name\":\"Knob\"}]}" : "{\"paired\":[]}";
        b.retain(topic, json);
        core.handleInbound(topic, json, now);
    }
    // What the HMI published, as the strip sees it.
    void receive(FakeBroker& b, LedBridgeCore& core, const LedBridgeCore::Outbound& msg, Millis now) {
        char id[17];
        LedTopic kind = LedTopic::Unknown;
        parseLedTopic(msg.topic, id, sizeof(id), kind);
        if (kind == LedTopic::Set) {
            std::strncpy(lastSet, msg.payload, sizeof(lastSet) - 1);
            char from[24] = {};
            unsigned long seq = 0;
            const char* f = std::strstr(msg.payload, "\"from\":\"");
            if (f != nullptr) std::sscanf(f + 8, "%23[^\"]", from);
            const char* q = std::strstr(msg.payload, "\"seq\":");
            if (q != nullptr) std::sscanf(q + 6, "%lu", &seq);
            if (!paired) {
                ++rejected;
                publishState(b, core, from, static_cast<uint32_t>(seq), true, now);
                return;
            }
            LedStateMsg m;
            decodeLedState(msg.payload, "nobody", m);
            if (m.fields & LedStatePower) tuple.power = m.state.power;
            if (m.fields & LedStateBrightness) tuple.brightness = m.state.brightness;
            if (m.fields & LedStateColor) tuple.rgb = m.state.rgb;
            if (m.fields & LedStateEffect) tuple.effect = m.state.effect;
            if (m.fields & LedStateParams) std::memcpy(tuple.params, m.state.params, kLedParamCount);
            if (m.fields & LedStateEffectColor) tuple.effectRgb = m.state.effectRgb;
            publishState(b, core, from, static_cast<uint32_t>(seq), false, now);
        } else if (kind == LedTopic::Pair) {
            const char* c = std::strstr(msg.payload, "\"code\":");
            if (c != nullptr) {
                int dialled = 0;
                std::sscanf(c + 7, "%d", &dialled);
                if (windowOpen && dialled == code) {
                    paired = everPaired = true;
                    windowOpen = false;
                    publishControllers(b, core, now);
                }
            } else if (!everPaired) {
                paired = everPaired = true;  // the first pairing is free
                publishControllers(b, core, now);
            } else {
                windowOpen = true;  // blink the code, wait for the confirm
            }
        } else if (kind == LedTopic::Unpair) {
            paired = false;
            publishControllers(b, core, now);
        }
    }
};

struct Rig {
    AppModel app;
    FakeBroker broker;
    FakeStrip strip;
    LedBridgeCore core{app.ledRegistry(), app.light(), kOwn};
    Millis now = 1000;
    // One display frame: inbound was delivered as it arrived; drain, then outbound to the strip.
    void frame(Millis at) {
        now = at;
        app.drainPendingMutations(now);
        LedBridgeCore::Outbound out;
        while (core.nextOutbound(out, now)) strip.receive(broker, core, out, now);
        core.tick(now);
    }
    void announceStrip() {
        char topic[64];
        formatLedTopic(topic, sizeof(topic), kStrip, LedTopic::Info);
        broker.retain(topic, "{\"id\":\"led-a1b2c3\",\"model\":\"esp32c6-led-web\",\"fw\":\"1.11\",\"name\":\"ESP32C6-LED-A1B2C4\",\"max_leds\":300,\"ip\":\"192.168.2.40\"}");
        formatLedTopic(topic, sizeof(topic), kStrip, LedTopic::Status);
        broker.retain(topic, "online");
        strip.tuple.power = true;
        strip.tuple.brightness = 60;
        strip.tuple.rgb = 0x00FF00;
        broker.replay(core, now);
        strip.publishState(broker, core, "device", 1, false, now);
    }
    int slot() const { return app.ledRegistry().find(kStrip); }
    void pairFresh() {
        core.startPairing(slot(), "Knob", now);
        frame(now + 16);
    }
    void select() {
        app.ledRegistry().select(slot());
        frame(now + 16);
    }
};

}  // namespace

TEST_CASE(an_announced_strip_is_available_but_not_mine) {
    Rig r;
    r.announceStrip();
    r.frame(1016);
    const LedSlot* s = r.app.ledRegistry().slot(r.slot());
    CHECK(s != nullptr);
    CHECK(s->online && !s->paired && s->stateKnown);
    CHECK(std::strcmp(s->name.c_str(), "ESP32C6-LED-A1B2C4") == 0);
    CHECK_EQ(s->state.brightness, 60);
    CHECK(!r.app.ledRegistry().commandsAllowed(r.slot()));
    r.select();
    CHECK_EQ(r.app.light().state().stripSlot, r.slot());
    CHECK_EQ(r.app.light().state().brightness, 60);  // followed, silently
    r.app.light().adjustBrightness(5);  // refused locally: nothing sent, nothing kept
    r.frame(1100);
    CHECK_EQ(r.strip.publishes, 1);
    CHECK_EQ(r.app.light().pendingFields(), 0u);
    CHECK(!r.app.light().publishPending());
}

TEST_CASE(a_fresh_strip_pairs_on_one_tap_and_becomes_mine) {
    Rig r;
    r.announceStrip();
    r.frame(1016);
    CHECK(r.core.startPairing(r.slot(), "Knob", r.now));
    CHECK(r.core.pairing() == LedBridgeCore::Pairing::Requested);
    r.frame(1032);
    CHECK(r.core.pairing() == LedBridgeCore::Pairing::Paired);
    CHECK(r.app.ledRegistry().slot(r.slot())->paired);
    CHECK(r.app.ledRegistry().commandsAllowed(r.slot()));
    CHECK(r.core.unpair(r.slot()));
    r.frame(1048);
    CHECK(!r.app.ledRegistry().slot(r.slot())->paired);
    CHECK(!r.strip.paired);
}

TEST_CASE(a_used_strip_needs_the_blink_code_and_a_wrong_one_times_out) {
    Rig r;
    r.strip.everPaired = true;
    r.announceStrip();
    r.frame(1016);
    CHECK(r.core.startPairing(r.slot(), "Knob", r.now));
    r.frame(1032);
    CHECK(r.core.pairing() == LedBridgeCore::Pairing::Requested);
    CHECK(r.strip.windowOpen);
    CHECK(!r.core.confirmPairing(9, r.now));
    CHECK(r.core.confirmPairing(2, r.now));  // the wrong code
    r.frame(1048);
    CHECK(r.core.pairing() == LedBridgeCore::Pairing::Confirming);
    r.frame(1048 + 60001);
    CHECK(r.core.pairing() == LedBridgeCore::Pairing::Failed);
    r.core.resetPairing();
    CHECK(r.core.startPairing(r.slot(), "Knob", r.now));
    r.frame(r.now + 16);
    CHECK(r.core.confirmPairing(3, r.now));
    r.frame(r.now + 16);
    CHECK(r.core.pairing() == LedBridgeCore::Pairing::Paired);
}

TEST_CASE(a_set_carries_only_the_changed_fields_and_its_echo_is_silent) {
    Rig r;
    r.announceStrip();
    r.frame(1016);
    r.pairFresh();
    r.select();
    r.app.mutations().post(LightModel::make(LightModel::Field::BrightnessDelta, 5, 1100));
    r.frame(1100);
    CHECK_EQ(r.core.setsPublished(), 1u);
    CHECK(std::strstr(r.strip.lastSet, "\"brightness\":165") != nullptr);
    CHECK(std::strstr(r.strip.lastSet, "\"color\"") == nullptr);
    CHECK(std::strstr(r.strip.lastSet, "\"from\":\"qnob-0a0b0c\",\"seq\":1}") != nullptr);
    CHECK_EQ(r.strip.tuple.brightness, 65);
    r.frame(1116);
    CHECK_EQ(r.app.light().state().brightness, 65);
    CHECK_EQ(r.app.light().pendingFields(), 0u);
    CHECK_EQ(r.core.setsPublished(), 1u);
    CHECK_EQ(r.strip.publishes, 2);
    CHECK_EQ(r.strip.rejected, 0);
}

TEST_CASE(a_drag_yields_at_most_ten_sets_per_second_and_the_last_value_lands) {
    Rig r;
    r.announceStrip();
    r.frame(1016);
    r.pairFresh();
    r.select();
    for (Millis t = 2000; t < 4000; t += 10) {
        r.app.light().adjustBrightness((t / 10) % 2 ? 2 : -1);
        r.frame(t);
    }
    r.frame(4200);
    CHECK(r.core.setsPublished() <= 22u);
    CHECK(r.core.setsPublished() >= 19u);
    CHECK_EQ(r.strip.tuple.brightness, r.app.light().state().brightness);
    CHECK_EQ(r.app.light().pendingFields(), 0u);
}

TEST_CASE(a_remote_change_is_applied_without_an_echo_publish) {
    Rig r;
    r.announceStrip();
    r.frame(1016);
    r.pairFresh();
    r.select();
    r.strip.tuple.power = false;
    r.strip.tuple.rgb = 0xFF00FF;
    r.strip.tuple.effect = LedEffect::Aurora;
    r.strip.publishState(r.broker, r.core, "web", 40, false, r.now);
    r.frame(1200);
    CHECK(!r.app.light().state().power);
    CHECK_EQ(r.app.light().state().rgb, 0xFF00FFu);
    CHECK(r.app.light().state().effect == LedEffect::Aurora);
    CHECK_EQ(r.core.setsPublished(), 0u);
    CHECK_EQ(r.app.light().pendingFields(), 0u);
}

TEST_CASE(the_lwt_marks_the_strip_offline_and_commands_are_refused_locally) {
    Rig r;
    r.announceStrip();
    r.frame(1016);
    r.pairFresh();
    r.select();
    char topic[64];
    formatLedTopic(topic, sizeof(topic), kStrip, LedTopic::Status);
    r.core.handleInbound(topic, "offline", r.now);
    r.frame(1200);
    CHECK(!r.app.ledRegistry().slot(r.slot())->online);
    r.app.light().adjustBrightness(-10);
    r.frame(1300);
    CHECK_EQ(r.core.setsPublished(), 0u);
    CHECK_EQ(r.app.light().pendingFields(), 0u);
    r.core.handleInbound(topic, "online", r.now);
    r.app.light().adjustBrightness(-10);
    r.frame(1400);
    CHECK_EQ(r.core.setsPublished(), 1u);
}

TEST_CASE(a_retained_replay_on_reconnect_changes_nothing) {
    Rig r;
    r.announceStrip();
    r.frame(1016);
    r.pairFresh();
    r.select();
    const LightState before = r.app.light().state();
    r.broker.replay(r.core, 5000);
    r.frame(5016);
    CHECK_EQ(r.app.ledRegistry().usedCount(), static_cast<size_t>(1));
    CHECK(r.app.ledRegistry().slot(r.slot())->paired);
    CHECK(r.app.light().state() == before);
    CHECK_EQ(r.core.setsPublished(), 0u);
    CHECK_EQ(r.core.malformedCount(), 0u);
}

TEST_CASE(the_ninth_strip_is_refused_and_counted) {
    Rig r;
    char topic[64];
    for (int i = 0; i < 9; ++i) {
        char id[17];
        std::snprintf(id, sizeof(id), "led-%06x", 0x100000 + i);
        formatLedTopic(topic, sizeof(topic), id, LedTopic::Info);
        char json[96];
        std::snprintf(json, sizeof(json), "{\"id\":\"%s\",\"name\":\"S%d\"}", id, i);
        r.core.handleInbound(topic, json, r.now);
    }
    CHECK_EQ(r.app.ledRegistry().usedCount(), static_cast<size_t>(8));
    CHECK_EQ(r.core.refusedAnnounceCount(), 1u);
    CHECK(!r.core.handleInbound("homio/other/x/info", "{}", r.now));
    r.core.handleInbound("homio/led/led-100000/state", "garbage", r.now);
    CHECK_EQ(r.core.malformedCount(), 1u);
}

TEST_CASE(the_notify_path_allocates_nothing) {
    Rig r;
    r.announceStrip();
    r.frame(1016);
    r.pairFresh();
    r.select();
    NO_HEAP_SCOPE();
    for (int i = 0; i < 200; ++i) {
        r.app.light().adjustBrightness(i % 2 ? 1 : -1);
        r.frame(2000 + i * 16);
    }
    CHECK(r.core.setsPublished() > 0);
}
