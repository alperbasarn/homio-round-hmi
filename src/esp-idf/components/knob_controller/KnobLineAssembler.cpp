#include "KnobLineAssembler.h"

#include <cstdlib>
#include <cstring>

void KnobLineAssembler::push(const Token& token) {
    if (count_ >= kQueueDepth) {
        ++overflow_;
        return;
    }
    queue_[(head_ + count_) % kQueueDepth] = token;
    ++count_;
}

bool KnobLineAssembler::next(Token& out) {
    if (count_ == 0) {
        return false;
    }
    out = queue_[head_];
    head_ = (head_ + 1) % kQueueDepth;
    --count_;
    return true;
}

void KnobLineAssembler::completeBareSign() {
    Token t;
    t.kind = Token::Kind::Delta;
    t.delta = line_[0] == '+' ? 1 : -1;
    t.text[0] = line_[0];
    t.text[1] = 0;
    push(t);
    len_ = 0;
    lineTruncated_ = false;
    signHeld_ = false;
}

void KnobLineAssembler::completeLine() {
    if (len_ == 0) {
        return;
    }
    line_[len_] = 0;
    Token t;
    std::memcpy(t.text, line_, len_ + 1);
    if (lineTruncated_) {
        ++truncated_;
    }
    if (isSign(static_cast<uint8_t>(line_[0]))) {
        if (len_ == 1) {
            t.kind = Token::Kind::Delta;
            t.delta = line_[0] == '+' ? 1 : -1;
        } else {
            bool digits = true;
            for (size_t i = 1; i < len_; ++i) {
                if (!isDigit(static_cast<uint8_t>(line_[i]))) {
                    digits = false;
                    break;
                }
            }
            if (digits && !lineTruncated_) {
                long value = std::strtol(line_ + 1, nullptr, 10);
                if (value > kClampPerToken) value = kClampPerToken;
                t.kind = Token::Kind::Delta;
                t.delta = static_cast<int>(line_[0] == '+' ? value : -value);
            } else {
                t.kind = Token::Kind::Message;  // "+abc": alive, no movement
            }
        }
    } else {
        t.kind = Token::Kind::Message;
    }
    push(t);
    len_ = 0;
    lineTruncated_ = false;
    signHeld_ = false;
}

void KnobLineAssembler::feed(const uint8_t* bytes, size_t len, uint32_t nowMs) {
    for (size_t i = 0; i < len; ++i) {
        const uint8_t c = bytes[i];
        if (isTerminator(c)) {
            completeLine();
            continue;
        }
        // A held bare sign followed by another sign was a legacy detent ("++" is two). Any other
        // byte keeps accumulating, so "+abc" ends as one message and "+12" as one count.
        if (len_ == 1 && isSign(static_cast<uint8_t>(line_[0])) && isSign(c)) {
            completeBareSign();
        }
        if (len_ < kMaxLine - 1) {
            line_[len_++] = static_cast<char>(c);
        } else {
            lineTruncated_ = true;
        }
    }
    if (len_ == 1 && isSign(static_cast<uint8_t>(line_[0]))) {
        if (!signHeld_) {
            signHeld_ = true;
            signSinceMs_ = nowMs;
        }
    } else {
        signHeld_ = false;
    }
}

void KnobLineAssembler::tick(uint32_t nowMs) {
    if (signHeld_ && len_ == 1 && static_cast<uint32_t>(nowMs - signSinceMs_) >= kBareSignHoldMs) {
        completeBareSign();
    }
}
