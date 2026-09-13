#include "media_target/PcProtocol.h"

#include <cstdio>
#include <cstring>

namespace media {
namespace pc {

namespace {

int finish(int n, size_t capacity) {
    return (n < 0 || static_cast<size_t>(n) >= capacity) ? -1 : n;
}

const char* const kKeys[] = {"state", "volume", "position", "duration", "title", "player"};
constexpr size_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

// Index of the known key that starts at p and is followed by a colon, or -1.
int keyAt(const char* p) {
    for (size_t i = 0; i < kKeyCount; ++i) {
        const size_t n = std::strlen(kKeys[i]);
        if (std::strncmp(p, kKeys[i], n) == 0 && p[n] == ':') {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The end of a value that started at p: the next comma followed by a known key, or the end of the
// text. A title keeps its own commas this way.
const char* valueEnd(const char* p) {
    const char* q = p;
    while (*q != '\0') {
        if (*q == ',' && keyAt(q + 1) >= 0) {
            return q;
        }
        ++q;
    }
    return q;
}

bool parseInt(const char* p, const char* end, long& out) {
    if (p >= end) {
        return false;
    }
    bool negative = false;
    if (*p == '-') {
        negative = true;
        ++p;
    }
    if (p >= end || *p < '0' || *p > '9') {
        return false;
    }
    long value = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        if (value > 2147483647L) {
            return false;
        }
        ++p;
    }
    while (p < end && (*p == ' ' || *p == '\t')) {
        ++p;
    }
    if (p != end) {
        return false;
    }
    out = negative ? -value : value;
    return true;
}

bool equalsSpan(const char* p, const char* end, const char* word) {
    const size_t n = std::strlen(word);
    return static_cast<size_t>(end - p) == n && std::strncmp(p, word, n) == 0;
}

}  // namespace

int encodeSetpoint(char* out, size_t capacity, int percent) {
    if (out == nullptr || capacity == 0 || percent < 0 || percent > 100) {
        return -1;
    }
    return finish(std::snprintf(out, capacity, "setpoint:%d", percent), capacity);
}

int encodeControl(char* out, size_t capacity, Control verb) {
    if (out == nullptr || capacity == 0) {
        return -1;
    }
    const char* word = "play";
    switch (verb) {
        case Control::Play: word = "play"; break;
        case Control::Pause: word = "pause"; break;
        case Control::Forward: word = "forward"; break;
        case Control::Rewind: word = "rewind"; break;
        case Control::Toggle: word = "toggle"; break;
        default: return -1;
    }
    return finish(std::snprintf(out, capacity, "%s", word), capacity);
}

int encodeStateRequest(char* out, size_t capacity, bool v2) {
    if (out == nullptr || capacity == 0) {
        return -1;
    }
    return finish(std::snprintf(out, capacity, "%s", v2 ? "request:v2" : "request"), capacity);
}

int encodeSeekAbsolute(char* out, size_t capacity, int percent) {
    if (out == nullptr || capacity == 0 || percent < 0 || percent > 100) {
        return -1;
    }
    return finish(std::snprintf(out, capacity, "seek_abs:%d", percent), capacity);
}

int encodeSeekRelative(char* out, size_t capacity, int32_t deltaMs) {
    if (out == nullptr || capacity == 0) {
        return -1;
    }
    return finish(std::snprintf(out, capacity, "seek_rel:%ld", static_cast<long>(deltaMs)), capacity);
}

const char* stripSender(const char* payload, char* senderOut, size_t senderCapacity) {
    if (senderOut != nullptr && senderCapacity > 0) {
        senderOut[0] = '\0';
    }
    if (payload == nullptr) {
        return "";
    }
    if (std::strncmp(payload, "sender=", 7) != 0) {
        return payload;
    }
    const char* comma = std::strchr(payload + 7, ',');
    if (comma == nullptr) {
        return "";  // an envelope with no body
    }
    if (senderOut != nullptr && senderCapacity > 0) {
        size_t n = static_cast<size_t>(comma - (payload + 7));
        if (n >= senderCapacity) {
            n = senderCapacity - 1;
        }
        std::memcpy(senderOut, payload + 7, n);
        senderOut[n] = '\0';
    }
    return comma + 1;
}

bool parseResponse(const char* payload, Response& out) {
    out = Response{};
    char sender[33];
    const char* body = stripSender(payload, sender, sizeof(sender));
    out.sender.assign(sender);
    while (*body == ' ' || *body == '\t') {
        ++body;
    }
    if (*body == '\0') {
        return false;
    }
    const char* end = body + std::strlen(body);
    while (end > body && (end[-1] == ' ' || end[-1] == '\r' || end[-1] == '\n' || end[-1] == '\t')) {
        --end;
    }
    // The bare words.
    if (equalsSpan(body, end, "playing") || equalsSpan(body, end, "unpaused")) {
        out.hasPlayState = true;
        out.playing = true;
        return true;
    }
    if (equalsSpan(body, end, "paused")) {
        out.hasPlayState = true;
        out.playing = false;
        return true;
    }
    // "setpoint:<n>": the volume as the PC reports it after a change.
    if (std::strncmp(body, "setpoint:", 9) == 0) {
        long value = 0;
        if (parseInt(body + 9, end, value) && value >= 0 && value <= 100) {
            out.hasVolume = true;
            out.volume = static_cast<int>(value);
            return true;
        }
        return false;
    }
    // "key:value" pairs in any order; a title keeps its commas.
    const char* p = body;
    while (p < end) {
        const int key = keyAt(p);
        if (key < 0) {
            const char* comma = std::strchr(p, ',');
            if (comma == nullptr || comma >= end) {
                break;
            }
            p = comma + 1;
            continue;
        }
        const char* value = p + std::strlen(kKeys[key]) + 1;
        const char* vend = valueEnd(value);
        if (vend > end) {
            vend = end;
        }
        long number = 0;
        switch (key) {
            case 0:
                if (equalsSpan(value, vend, "playing")) { out.hasPlayState = true; out.playing = true; }
                else if (equalsSpan(value, vend, "paused")) { out.hasPlayState = true; out.playing = false; }
                break;
            case 1:
                if (parseInt(value, vend, number) && number >= 0 && number <= 100) { out.hasVolume = true; out.volume = static_cast<int>(number); }
                break;
            case 2:
                if (parseInt(value, vend, number) && number >= 0) { out.hasPosition = true; out.positionMs = static_cast<int32_t>(number); out.v2 = true; }
                break;
            case 3:
                if (parseInt(value, vend, number) && number >= 0) { out.hasDuration = true; out.durationMs = static_cast<int32_t>(number); out.v2 = true; }
                break;
            case 4:
                out.hasTitle = true;
                out.title.assign(value, static_cast<size_t>(vend - value));
                break;
            case 5:
                out.hasPlayer = true;
                out.player.assign(value, static_cast<size_t>(vend - value));
                break;
            default:
                break;
        }
        p = (*vend == ',') ? vend + 1 : vend;
    }
    return out.any();
}

}  // namespace pc
}  // namespace media
