#include "model/RateLimit.h"

namespace model {

RateLimiter::RateLimiter(Millis intervalMs)
    : interval_(intervalMs), lastPublish_(0), pending_(false), everPublished_(false) {}

bool RateLimiter::shouldPublish(Millis nowMs) {
    if (!pending_) {
        return false;
    }
    if (everPublished_ && nowMs - lastPublish_ < interval_) {
        return false;
    }
    lastPublish_ = nowMs;
    pending_ = false;
    everPublished_ = true;
    return true;
}

bool RateLimiter::flush() {
    if (!pending_) {
        return false;
    }
    pending_ = false;
    return true;
}

void RateLimiter::reset() {
    lastPublish_ = 0;
    pending_ = false;
    everPublished_ = false;
}

Debouncer::Debouncer(Millis settleMs) : settle_(settleMs), lastPoke_(0), armed_(false) {}

void Debouncer::poke(Millis nowMs) {
    lastPoke_ = nowMs;
    armed_ = true;
}

bool Debouncer::settled(Millis nowMs) {
    if (!armed_) {
        return false;
    }
    if (nowMs - lastPoke_ < settle_) {
        return false;
    }
    armed_ = false;
    return true;
}

EchoSuppressor::EchoSuppressor(Millis windowMs)
    : window_(windowMs), publishedAt_(0), value_(0), valid_(false) {}

void EchoSuppressor::published(int32_t value, Millis nowMs) {
    value_ = value;
    publishedAt_ = nowMs;
    valid_ = true;
}

bool EchoSuppressor::isEcho(int32_t value, Millis nowMs) const {
    if (!valid_ || value != value_) {
        return false;
    }
    return nowMs - publishedAt_ <= window_;
}

void EchoSuppressor::clear() {
    valid_ = false;
}

}  // namespace model
