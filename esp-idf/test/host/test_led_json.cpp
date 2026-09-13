// test_led_json (LED-03): the topic family, the controller id rule, and the fixed-buffer codec
// against the documents of dev_notes/led_mqtt_contract.md, malformed input included, no heap.
#include <cstring>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "adapters/LedJson.h"
#include "adapters/LedTopics.h"

using namespace adapters;
using namespace model;

namespace {
constexpr const char* kOwn = "qnob-0a0b0c";
constexpr const char* kState =
    "{\"power\":true,\"brightness\":204,\"color\":\"#FF4858\",\"effect\":\"fire\","
    "\"effect_params\":[10,20,30,40,50],\"effect_color\":\"#00ff00\",\"count\":60,\"src\":\"qnob-0a0b0c\",\"seq\":12}";
}  // namespace

TEST_CASE(topics_parse_and_format_both_ways) {
    char id[17];
    LedTopic kind = LedTopic::Unknown;
    CHECK(parseLedTopic("homio/led/led-a1b2c3/state", id, sizeof(id), kind));
    CHECK(kind == LedTopic::State);
    CHECK(std::strcmp(id, "led-a1b2c3") == 0);
    CHECK(parseLedTopic("homio/led/led-a1b2c3/weird", id, sizeof(id), kind));  // family, unknown leaf
    CHECK(kind == LedTopic::Unknown);
    CHECK(!parseLedTopic("homio/other/led-a1b2c3/state", id, sizeof(id), kind));
    CHECK(!parseLedTopic("homio/led//state", id, sizeof(id), kind));
    CHECK(!parseLedTopic("homio/led/led-a1b2c3/state/extra", id, sizeof(id), kind));
    CHECK(!parseLedTopic("homio/led/this-id-is-far-too-long-to-fit/state", id, sizeof(id), kind));
    char topic[64];
    CHECK_EQ(formatLedTopic(topic, sizeof(topic), "led-a1b2c3", LedTopic::Set), 24);
    CHECK(std::strcmp(topic, "homio/led/led-a1b2c3/set") == 0);
    CHECK_EQ(formatLedTopic(topic, 10, "led-a1b2c3", LedTopic::Set), -1);
    CHECK(formatLedSubscription(topic, sizeof(topic), LedTopic::Info) > 0);
    CHECK(std::strcmp(topic, "homio/led/+/info") == 0);
    const uint8_t mac[6] = {0x1c, 0xdb, 0xd4, 0x0a, 0x0b, 0x0c};
    CHECK(makeControllerId(mac, id, sizeof(id)));
    CHECK(std::strcmp(id, kOwn) == 0);
}

TEST_CASE(info_and_status_decode) {
    LedInfoMsg info;
    CHECK(decodeLedInfo("{\"id\":\"led-a1b2c3\",\"model\":\"esp32c6-led-web\",\"fw\":\"1.11\",\"name\":\"ESP32C6-LED-A1B2C4\",\"max_leds\":300,\"ip\":\"192.168.2.40\"}", info));
    CHECK(std::strcmp(info.id.c_str(), "led-a1b2c3") == 0);
    CHECK(std::strcmp(info.name.c_str(), "ESP32C6-LED-A1B2C4") == 0);
    CHECK_EQ(info.maxLeds, 300);
    CHECK(std::strcmp(info.ip.c_str(), "192.168.2.40") == 0);
    CHECK(!decodeLedInfo("{\"name\":\"no id\"}", info));
    CHECK(!decodeLedInfo("not json", info));
    bool online = false;
    CHECK(decodeLedStatus("online", online) && online);
    CHECK(decodeLedStatus("\"offline\"", online) && !online);
    CHECK(!decodeLedStatus("sleeping", online));
}

TEST_CASE(state_decodes_in_hmi_units_with_source_and_seq) {
    LedStateMsg m;
    CHECK(decodeLedState(kState, kOwn, m));
    CHECK(m.state.power);
    CHECK_EQ(m.state.brightness, 80);  // 204 on the wire
    CHECK_EQ(m.state.rgb, 0xFF4858u);
    CHECK(m.state.effect == LedEffect::Fire);
    CHECK_EQ(m.state.params[4], 50);
    CHECK_EQ(m.state.effectRgb, 0x00FF00u);
    CHECK_EQ(m.state.count, 60);
    CHECK(m.source == LedSource::Self);
    CHECK_EQ(m.seq, 12u);
    CHECK(!m.rejected);
    CHECK_EQ(m.fields & LedStateBrightness, static_cast<uint16_t>(LedStateBrightness));
    // Partial, odd spacing, a rejected flag, a web source, a bad colour and an unknown effect.
    CHECK(decodeLedState(" { \"brightness\" : 0 , \"color\":\"red\", \"effect\":\"disco\", \"src\":\"web\", \"rejected\":true } ", kOwn, m));
    CHECK_EQ(m.state.brightness, 0);
    CHECK_EQ(m.fields & LedStateColor, 0);
    CHECK_EQ(m.fields & LedStateEffect, 0);
    CHECK(m.source == LedSource::Web);
    CHECK(m.rejected);
    CHECK(decodeLedState("{\"src\":\"qnob-ffffff\",\"seq\":3}", kOwn, m));
    CHECK(m.source == LedSource::OtherController);
    CHECK(!decodeLedState("[1,2]", kOwn, m));
}

TEST_CASE(controllers_decode_and_find_this_hmi) {
    LedControllersMsg c;
    CHECK(decodeLedControllers("{\"paired\":[{\"id\":\"qnob-111111\",\"name\":\"Hall\"},{\"id\":\"qnob-0a0b0c\",\"name\":\"Desk \\\"knob\\\"\"}]}", kOwn, c));
    CHECK_EQ(c.count, static_cast<size_t>(2));
    CHECK(c.listsOwnId);
    CHECK(std::strcmp(c.names[1].c_str(), "Desk \"knob\"") == 0);
    CHECK(decodeLedControllers("{\"paired\":[]}", kOwn, c));
    CHECK_EQ(c.count, static_cast<size_t>(0));
    CHECK(!c.listsOwnId);
    CHECK(!decodeLedControllers("{\"paired\":\"none\"}", kOwn, c));
    CHECK(!decodeLedControllers("{\"paired\":[{\"id\":\"x\"", kOwn, c));
}

TEST_CASE(set_pair_and_unpair_encode_only_what_they_must) {
    char out[256];
    LedStripState s;
    s.power = true;
    s.brightness = 50;
    s.rgb = 0x0000FF;
    s.effect = LedEffect::Glow;
    const int n = encodeLedSet(out, sizeof(out), s, LightModel::PendingBrightness | LightModel::PendingEffect, kOwn, 7);
    CHECK(n > 0);
    CHECK(std::strcmp(out, "{\"brightness\":127,\"effect\":\"glow\",\"from\":\"qnob-0a0b0c\",\"seq\":7}") == 0);
    CHECK_EQ(encodeLedSet(out, sizeof(out), s, 0, kOwn, 8), -1);  // nothing settable
    CHECK_EQ(encodeLedSet(out, 20, s, LightModel::PendingBrightness, kOwn, 9), -1);  // too small
    CHECK(encodeLedPair(out, sizeof(out), kOwn, "Kit\"chen") > 0);
    CHECK(std::strcmp(out, "{\"controller\":\"qnob-0a0b0c\",\"name\":\"Kit\\\"chen\"}") == 0);
    CHECK(encodeLedPairConfirm(out, sizeof(out), kOwn, 4) > 0);
    CHECK(std::strcmp(out, "{\"controller\":\"qnob-0a0b0c\",\"code\":4}") == 0);
    CHECK_EQ(encodeLedPairConfirm(out, sizeof(out), kOwn, 7), -1);
    CHECK(encodeLedUnpair(out, sizeof(out), kOwn) > 0);
    CHECK(std::strcmp(out, "{\"controller\":\"qnob-0a0b0c\"}") == 0);
}

TEST_CASE(codec_allocates_nothing) {
    NO_HEAP_SCOPE();
    LedStateMsg m;
    char out[256];
    for (int i = 0; i < 1000; ++i) {
        CHECK(decodeLedState(kState, kOwn, m));
        CHECK(encodeLedSet(out, sizeof(out), m.state, 0x3F, kOwn, static_cast<uint32_t>(i)) > 0);
    }
}
