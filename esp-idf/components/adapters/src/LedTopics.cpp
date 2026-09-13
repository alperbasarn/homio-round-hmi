#include "adapters/LedTopics.h"

#include <cstdio>
#include <cstring>

namespace adapters {

namespace {

const char* const kLeaves[] = {"info", "status", "state", "set", "pair", "unpair", "controllers"};
constexpr size_t kLeafCount = sizeof(kLeaves) / sizeof(kLeaves[0]);

}  // namespace

const char* ledTopicLeaf(LedTopic kind) {
    const size_t i = static_cast<size_t>(kind);
    return i < kLeafCount ? kLeaves[i] : "";
}

bool parseLedTopic(const char* topic, char* idOut, size_t idCapacity, LedTopic& kind) {
    kind = LedTopic::Unknown;
    if (topic == nullptr || idOut == nullptr || idCapacity == 0) {
        return false;
    }
    const size_t prefixLen = std::strlen(kLedTopicPrefix);
    if (std::strncmp(topic, kLedTopicPrefix, prefixLen) != 0) {
        return false;
    }
    const char* idStart = topic + prefixLen;
    const char* slash = std::strchr(idStart, '/');
    if (slash == nullptr || slash == idStart) {
        return false;
    }
    const size_t idLen = static_cast<size_t>(slash - idStart);
    if (idLen + 1 > idCapacity) {
        return false;
    }
    const char* leaf = slash + 1;
    if (*leaf == '\0' || std::strchr(leaf, '/') != nullptr) {
        return false;
    }
    std::memcpy(idOut, idStart, idLen);
    idOut[idLen] = '\0';
    for (size_t i = 0; i < kLeafCount; ++i) {
        if (std::strcmp(leaf, kLeaves[i]) == 0) {
            kind = static_cast<LedTopic>(i);
            return true;
        }
    }
    return true;  // in the family, unknown leaf: the caller ignores it
}

int formatLedTopic(char* out, size_t capacity, const char* id, LedTopic kind) {
    if (out == nullptr || capacity == 0 || id == nullptr) {
        return -1;
    }
    const int n = std::snprintf(out, capacity, "%s%s/%s", kLedTopicPrefix, id, ledTopicLeaf(kind));
    return (n < 0 || static_cast<size_t>(n) >= capacity) ? -1 : n;
}

int formatLedSubscription(char* out, size_t capacity, LedTopic kind) {
    if (out == nullptr || capacity == 0) {
        return -1;
    }
    const int n = std::snprintf(out, capacity, "%s+/%s", kLedTopicPrefix, ledTopicLeaf(kind));
    return (n < 0 || static_cast<size_t>(n) >= capacity) ? -1 : n;
}

bool makeControllerId(const uint8_t mac[6], char* out, size_t capacity) {
    if (mac == nullptr || out == nullptr || capacity < 12) {
        return false;
    }
    const int n = std::snprintf(out, capacity, "qnob-%02x%02x%02x", mac[3], mac[4], mac[5]);
    return n == 11;
}

}  // namespace adapters
