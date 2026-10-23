#pragma once

// PCKNOB-09 / PCKNOB-11 (D6): the accumulating line assembler for the knob link. Pure: no UART,
// no RTOS, no heap. Bytes go in, tokens come out of a small FIFO:
//   "+N\n" / "-N\n"  -> Delta (clamped to +-10 per token, D5)
//   "+"  / "-"        -> Delta +-1 (the legacy knob firmware sends bare signs with no terminator)
//   anything else     -> Message (the text, for the command consumers; "+abc" is a message, delta 0)
// A bare sign at the end of a chunk is held for kBareSignHoldMs so a "+12" split across two polls
// is not mistaken for a legacy "+" followed by garbage; at 9600 baud a full line takes ~4 ms.

#include <cstddef>
#include <cstdint>

class KnobLineAssembler {
public:
    static constexpr size_t kMaxLine = 64;
    static constexpr size_t kQueueDepth = 8;
    static constexpr uint32_t kBareSignHoldMs = 25;
    static constexpr int kClampPerToken = 10;

    struct Token {
        enum class Kind : uint8_t { None = 0, Delta, Message };
        Kind kind = Kind::None;
        int delta = 0;
        char text[kMaxLine] = {};
    };

    void feed(const uint8_t* bytes, size_t len, uint32_t nowMs);
    void tick(uint32_t nowMs);
    bool next(Token& out);

    size_t pending() const { return count_; }
    uint32_t overflow() const { return overflow_; }
    uint32_t truncated() const { return truncated_; }

private:
    static bool isDigit(uint8_t c) { return c >= '0' && c <= '9'; }
    static bool isSign(uint8_t c) { return c == '+' || c == '-'; }
    static bool isTerminator(uint8_t c) { return c == '\n' || c == '\r'; }
    void completeLine();
    void completeBareSign();
    void push(const Token& token);

    char line_[kMaxLine] = {};
    size_t len_ = 0;
    bool lineTruncated_ = false;
    bool signHeld_ = false;
    uint32_t signSinceMs_ = 0;

    Token queue_[kQueueDepth] = {};
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t overflow_ = 0;
    uint32_t truncated_ = 0;
};
