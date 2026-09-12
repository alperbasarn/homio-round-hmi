#pragma once
// Stand-in for the MQTT client used by the bridge and poller tests (NET-31). Records what was
// published and lets a test inject inbound messages. Allocates, so use it outside NoHeapScope.
#include <string>
#include <vector>

namespace fakes {

struct FakeTransport {
    struct Message {
        std::string topic;
        std::string payload;
    };

    bool connected = true;
    std::vector<Message> published;

    bool publish(const std::string& topic, const std::string& payload) {
        if (!connected) {
            return false;
        }
        published.push_back(Message{topic, payload});
        return true;
    }

    size_t countFor(const std::string& topic) const {
        size_t n = 0;
        for (const Message& m : published) {
            if (m.topic == topic) {
                ++n;
            }
        }
        return n;
    }

    const Message* last() const { return published.empty() ? nullptr : &published.back(); }
};

}  // namespace fakes
