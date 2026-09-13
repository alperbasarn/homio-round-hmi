// test_pc_protocol (MEDIA-04 / MEDIA-05): the three v1 bodies byte for byte, the v2 verbs, and
// the response grammar: bare words, setpoint, state pairs in any order, the sender envelope, a
// comma in the title, out-of-range values that never apply, empty and garbage payloads, no heap.
#include <cstring>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "media_target/PcProtocol.h"

using namespace media::pc;

TEST_CASE(the_v1_bodies_are_byte_identical_and_the_v2_verbs_encode) {
    char out[kBodyCapacity];
    CHECK_EQ(encodeSetpoint(out, sizeof(out), 42), 11);
    CHECK(std::strcmp(out, "setpoint:42") == 0);
    CHECK_EQ(encodeSetpoint(out, sizeof(out), 101), -1);
    CHECK_EQ(encodeSetpoint(out, 5, 42), -1);
    CHECK(encodeControl(out, sizeof(out), Control::Play) == 4 && std::strcmp(out, "play") == 0);
    CHECK(encodeControl(out, sizeof(out), Control::Pause) == 5 && std::strcmp(out, "pause") == 0);
    CHECK(encodeControl(out, sizeof(out), Control::Toggle) > 0 && std::strcmp(out, "toggle") == 0);
    CHECK(encodeStateRequest(out, sizeof(out)) == 7 && std::strcmp(out, "request") == 0);
    CHECK(encodeSeekAbsolute(out, sizeof(out), 37) > 0 && std::strcmp(out, "seek_abs:37") == 0);
    CHECK_EQ(encodeSeekAbsolute(out, sizeof(out), -1), -1);
    CHECK(encodeSeekRelative(out, sizeof(out), -5000) > 0 && std::strcmp(out, "seek_rel:-5000") == 0);
}

TEST_CASE(bare_words_and_setpoint_replies_parse) {
    Response r;
    CHECK(parseResponse("playing", r) && r.hasPlayState && r.playing && !r.hasVolume);
    CHECK(parseResponse("unpaused", r) && r.playing);
    CHECK(parseResponse(" paused\r\n", r) && r.hasPlayState && !r.playing);
    CHECK(parseResponse("setpoint:63", r) && r.hasVolume && r.volume == 63 && !r.hasPlayState);
    CHECK(!parseResponse("setpoint:150", r));
    CHECK(!parseResponse("setpoint:abc", r));
    CHECK(!parseResponse("", r));
    CHECK(!parseResponse("   ", r));
    CHECK(!parseResponse("garbage without colons", r));
    CHECK(!parseResponse(nullptr, r));
}

TEST_CASE(state_pairs_parse_in_any_order_with_the_envelope_and_a_comma_in_the_title) {
    Response r;
    CHECK(parseResponse("state:playing,volume:20", r));
    CHECK(r.hasPlayState && r.playing && r.hasVolume && r.volume == 20 && !r.v2);
    CHECK(parseResponse("sender=pc-1,state:paused,volume:5", r));
    CHECK(std::strcmp(r.sender.c_str(), "pc-1") == 0);
    CHECK(r.hasPlayState && !r.playing && r.volume == 5);
    CHECK(parseResponse("volume:77,state:playing", r));
    CHECK(r.playing && r.volume == 77);
    CHECK(parseResponse("state:playing,volume:40,position:12000,duration:180000,title:Hey, Jude,player:Spotify", r));
    CHECK(r.v2 && r.hasPosition && r.positionMs == 12000 && r.hasDuration && r.durationMs == 180000);
    CHECK(r.hasTitle && std::strcmp(r.title.c_str(), "Hey, Jude") == 0);
    CHECK(r.hasPlayer && std::strcmp(r.player.c_str(), "Spotify") == 0);
    // A bad value never applies and never spoils the rest.
    CHECK(parseResponse("state:playing,volume:200", r));
    CHECK(r.hasPlayState && r.playing && !r.hasVolume);
    CHECK(parseResponse("state:sideways,volume:30", r));
    CHECK(!r.hasPlayState && r.hasVolume && r.volume == 30);
    CHECK(parseResponse("state:paused,tempo:120,volume:1", r));
    CHECK(!r.playing && r.volume == 1);
    CHECK(parseResponse("sender=pc-9,", r) == false);  // an envelope with no body
    char sender[8];
    CHECK(std::strcmp(stripSender("sender=abcdefghijk,playing", sender, sizeof(sender)), "playing") == 0);
    CHECK(std::strcmp(sender, "abcdefg") == 0);  // truncated to fit
}

TEST_CASE(the_codec_allocates_nothing) {
    NO_HEAP_SCOPE();
    Response r;
    char out[kBodyCapacity];
    for (int i = 0; i < 2000; ++i) {
        CHECK(parseResponse("sender=pc,state:playing,volume:40,position:12000,duration:180000,title:A, B,player:P", r));
        CHECK(encodeSetpoint(out, sizeof(out), i % 101) > 0);
    }
}
