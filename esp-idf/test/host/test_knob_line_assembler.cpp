// PCKNOB-09 / PCKNOB-11: the knob line assembler on the host. Legacy bare signs, the +N/-N form,
// the D5 clamp, split lines, garbage, the bare-sign hold, truncation and the FIFO cap.
#include <cstring>

#include "HeapGuard.h"
#include "KnobLineAssembler.h"
#include "TestRunner.h"

namespace {

using Token = KnobLineAssembler::Token;

void feedText(KnobLineAssembler& a, const char* text, uint32_t nowMs) {
    a.feed(reinterpret_cast<const uint8_t*>(text), std::strlen(text), nowMs);
}

size_t drain(KnobLineAssembler& a, Token* out, size_t cap) {
    size_t n = 0;
    Token t;
    while (a.next(t)) {
        if (n < cap) out[n] = t;
        ++n;
    }
    return n;
}

}  // namespace

TEST_CASE(legacy_bare_signs_in_one_chunk_become_one_delta_each) {
    KnobLineAssembler a;
    NO_HEAP_SCOPE();
    feedText(a, "++-+", 0);  // the D6 failure: "++" used to match nothing
    a.tick(100);
    Token t[8];
    const size_t n = drain(a, t, 8);
    CHECK_EQ(n, static_cast<size_t>(4));
    CHECK(t[0].kind == Token::Kind::Delta && t[0].delta == 1);
    CHECK(t[1].kind == Token::Kind::Delta && t[1].delta == 1);
    CHECK(t[2].kind == Token::Kind::Delta && t[2].delta == -1);
    CHECK(t[3].kind == Token::Kind::Delta && t[3].delta == 1);
}

TEST_CASE(terminated_counts_parse_and_clamp) {
    KnobLineAssembler a;
    feedText(a, "+3\n-1\n+\n+abc\n+99999\n", 0);
    Token t[8];
    const size_t n = drain(a, t, 8);
    CHECK_EQ(n, static_cast<size_t>(5));
    CHECK(t[0].kind == Token::Kind::Delta && t[0].delta == 3);
    CHECK(t[1].kind == Token::Kind::Delta && t[1].delta == -1);
    CHECK(t[2].kind == Token::Kind::Delta && t[2].delta == 1);
    CHECK(t[3].kind == Token::Kind::Message && std::strcmp(t[3].text, "+abc") == 0);
    CHECK(t[4].kind == Token::Kind::Delta && t[4].delta == 10);  // D5 clamp
}

TEST_CASE(a_line_split_across_polls_is_one_token) {
    KnobLineAssembler a;
    Token t[8];
    feedText(a, "+1", 0);      // the poll caught the line mid-way
    a.tick(10);                // 10 ms later: still inside the hold, no token yet
    CHECK_EQ(drain(a, t, 8), static_cast<size_t>(0));
    feedText(a, "2\n", 12);
    CHECK_EQ(drain(a, t, 8), static_cast<size_t>(1));
    CHECK(t[0].kind == Token::Kind::Delta && t[0].delta == 10);  // "+12" clamped to 10
}

TEST_CASE(a_lone_bare_sign_completes_after_the_hold) {
    KnobLineAssembler a;
    Token t[8];
    feedText(a, "+", 0);
    a.tick(20);
    CHECK_EQ(drain(a, t, 8), static_cast<size_t>(0));
    a.tick(25);
    CHECK_EQ(drain(a, t, 8), static_cast<size_t>(1));
    CHECK(t[0].kind == Token::Kind::Delta && t[0].delta == 1);
}

TEST_CASE(messages_and_replies_come_through_as_text) {
    KnobLineAssembler a;
    Token t[8];
    feedText(a, "setpoint=42\nsp:17\r\n", 0);
    CHECK_EQ(drain(a, t, 8), static_cast<size_t>(2));
    CHECK(t[0].kind == Token::Kind::Message && std::strcmp(t[0].text, "setpoint=42") == 0);
    CHECK(t[1].kind == Token::Kind::Message && std::strcmp(t[1].text, "sp:17") == 0);
}

TEST_CASE(fifty_detents_in_a_second_are_fifty_tokens_with_no_loss_when_drained) {
    KnobLineAssembler a;
    int sum = 0;
    for (int i = 0; i < 50; ++i) {
        feedText(a, (i % 3 == 0) ? "-1\n" : "+1\n", static_cast<uint32_t>(i * 20));
        Token t;
        while (a.next(t)) sum += t.delta;  // the display task drains every frame
    }
    CHECK_EQ(sum, 50 - 2 * 17);
    CHECK_EQ(a.overflow(), 0u);
}

TEST_CASE(fifo_overflow_counts_when_nobody_drains) {
    KnobLineAssembler a;
    for (int i = 0; i < 12; ++i) feedText(a, "+1\n", 0);
    CHECK_EQ(a.pending(), KnobLineAssembler::kQueueDepth);
    CHECK_EQ(a.overflow(), 4u);
}

TEST_CASE(over_long_lines_are_truncated_not_overflowed) {
    KnobLineAssembler a;
    char big[300];
    std::memset(big, 'x', sizeof(big));
    big[298] = '\n';
    big[299] = 0;
    feedText(a, big, 0);
    Token t[8];
    CHECK_EQ(drain(a, t, 8), static_cast<size_t>(1));
    CHECK(t[0].kind == Token::Kind::Message);
    CHECK_EQ(std::strlen(t[0].text), KnobLineAssembler::kMaxLine - 1);
    CHECK_EQ(a.truncated(), 1u);
}
