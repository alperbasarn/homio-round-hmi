#pragma once
// LedTopics (LED-03): the `homio/led/<id>/<leaf>` topic family of the contract, parsed and built
// without allocation, plus the controller id rule (`qnob-` + the last three STA MAC bytes).
#include <cstddef>
#include <cstdint>

namespace adapters {

enum class LedTopic : uint8_t { Info = 0, Status, State, Set, Pair, Unpair, Controllers, Unknown };

constexpr const char* kLedTopicPrefix = "homio/led/";
constexpr size_t kLedTopicCapacity = 64;
constexpr size_t kLedControllerIdCapacity = 16;

const char* ledTopicLeaf(LedTopic kind);
// `homio/led/<id>/<leaf>`: the id (at most idCapacity - 1 characters) and the leaf; false when the
// topic is not in the family or the id does not fit.
bool parseLedTopic(const char* topic, char* idOut, size_t idCapacity, LedTopic& kind);
int formatLedTopic(char* out, size_t capacity, const char* id, LedTopic kind);
// "homio/led/+/<leaf>": the subscription for one leaf.
int formatLedSubscription(char* out, size_t capacity, LedTopic kind);
// "qnob-" + the last three MAC bytes, lower-case hex.
bool makeControllerId(const uint8_t mac[6], char* out, size_t capacity);

}  // namespace adapters
