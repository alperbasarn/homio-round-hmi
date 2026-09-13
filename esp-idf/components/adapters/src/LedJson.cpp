#include "adapters/LedJson.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace adapters {

namespace {

// ---- a flat JSON reader: no heap, no copies, the text is walked in place --------------------

const char* skipWs(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        ++p;
    }
    return p;
}

// p at the opening quote; returns the position after the closing quote, or nullptr.
const char* skipString(const char* p) {
    if (*p != '"') {
        return nullptr;
    }
    ++p;
    while (*p != '\0') {
        if (*p == '\134') {
            if (p[1] == '\0') {
                return nullptr;
            }
            p += 2;
            continue;
        }
        if (*p == '"') {
            return p + 1;
        }
        ++p;
    }
    return nullptr;
}

const char* skipValue(const char* p) {
    p = skipWs(p);
    if (*p == '"') {
        return skipString(p);
    }
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p != '\0') {
            if (*p == '"') {
                p = skipString(p);
                if (p == nullptr) {
                    return nullptr;
                }
                continue;
            }
            if (*p == '{' || *p == '[') {
                ++depth;
            } else if (*p == '}' || *p == ']') {
                --depth;
                if (depth == 0) {
                    return p + 1;
                }
            }
            ++p;
        }
        return nullptr;
    }
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' &&
           *p != '\t') {
        ++p;
    }
    return p;
}

// The value of `key` in the object starting at json, or nullptr.
const char* findValue(const char* json, const char* key) {
    if (json == nullptr || key == nullptr) {
        return nullptr;
    }
    const char* p = skipWs(json);
    if (*p != '{') {
        return nullptr;
    }
    ++p;
    const size_t keyLen = std::strlen(key);
    while (true) {
        p = skipWs(p);
        if (*p != '"') {
            return nullptr;
        }
        const char* k = p + 1;
        const char* afterKey = skipString(p);
        if (afterKey == nullptr) {
            return nullptr;
        }
        p = skipWs(afterKey);
        if (*p != ':') {
            return nullptr;
        }
        p = skipWs(p + 1);
        if (std::strncmp(k, key, keyLen) == 0 && k[keyLen] == '"') {
            return p;
        }
        p = skipValue(p);
        if (p == nullptr) {
            return nullptr;
        }
        p = skipWs(p);
        if (*p != ',') {
            return nullptr;
        }
        ++p;
    }
}

bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// Copies a string value, unescaped and truncated to fit. -1 when the value is not a string.
int readString(const char* v, char* out, size_t capacity) {
    if (v == nullptr || *v != '"' || out == nullptr || capacity == 0) {
        return -1;
    }
    ++v;
    size_t n = 0;
    while (*v != '\0' && *v != '"') {
        char c = *v;
        if (c == '\134') {
            ++v;
            if (*v == '\0') {
                return -1;
            }
            switch (*v) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'u': {
                    c = '?';
                    int digits = 0;
                    while (digits < 4 && isHex(v[1])) {
                        ++v;
                        ++digits;
                    }
                    break;
                }
                default: c = *v; break;
            }
        }
        if (n + 1 < capacity) {
            out[n++] = c;
        }
        ++v;
    }
    if (*v != '"') {
        return -1;
    }
    out[n] = '\0';
    return static_cast<int>(n);
}

bool readInt(const char* v, int32_t& out) {
    if (v == nullptr) {
        return false;
    }
    bool negative = false;
    if (*v == '-') {
        negative = true;
        ++v;
    }
    if (*v < '0' || *v > '9') {
        return false;
    }
    int64_t value = 0;
    while (*v >= '0' && *v <= '9') {
        value = value * 10 + (*v - '0');
        if (value > 2147483647LL) {
            return false;
        }
        ++v;
    }
    out = static_cast<int32_t>(negative ? -value : value);
    return true;
}

bool readBool(const char* v, bool& out) {
    if (v == nullptr) {
        return false;
    }
    if (std::strncmp(v, "true", 4) == 0) { out = true; return true; }
    if (std::strncmp(v, "false", 5) == 0) { out = false; return true; }
    int32_t n = 0;
    if (readInt(v, n)) { out = n != 0; return true; }
    return false;
}

// Reads up to max numbers from an array value; count is what was read.
bool readIntArray(const char* v, int32_t* out, size_t max, size_t& count) {
    count = 0;
    if (v == nullptr || *v != '[') {
        return false;
    }
    const char* p = skipWs(v + 1);
    while (*p != ']') {
        int32_t n = 0;
        if (!readInt(p, n)) {
            return false;
        }
        if (count < max) {
            out[count] = n;
        }
        ++count;
        p = skipValue(p);
        if (p == nullptr) {
            return false;
        }
        p = skipWs(p);
        if (*p == ',') {
            p = skipWs(p + 1);
        }
    }
    return true;
}

template <size_t Capacity>
bool readFixed(const char* v, model::FixedString<Capacity>& out) {
    char buffer[Capacity + 1];
    const int n = readString(v, buffer, sizeof(buffer));
    if (n < 0) {
        return false;
    }
    out.assign(buffer);
    return true;
}

// Appends formatted text at *n; keeps *n at -1 once the buffer overflowed.
void append(char* out, size_t capacity, int* n, const char* fmt, ...) {
    if (*n < 0 || static_cast<size_t>(*n) >= capacity) {
        *n = -1;
        return;
    }
    va_list args;
    va_start(args, fmt);
    const int written = std::vsnprintf(out + *n, capacity - static_cast<size_t>(*n), fmt, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(*n + written) >= capacity) {
        *n = -1;
        return;
    }
    *n += written;
}

// A JSON string literal with quotes and backslashes escaped.
void appendQuoted(char* out, size_t capacity, int* n, const char* text) {
    append(out, capacity, n, "\"");
    for (const char* p = text != nullptr ? text : ""; *p != '\0' && *n >= 0; ++p) {
        if (*p == '"' || *p == '\134') {
            append(out, capacity, n, "\134%c", *p);
        } else if (static_cast<unsigned char>(*p) < 0x20) {
            append(out, capacity, n, " ");
        } else {
            append(out, capacity, n, "%c", *p);
        }
    }
    append(out, capacity, n, "\"");
}

}  // namespace

bool parseHexColor(const char* text, uint32_t& rgb) {
    if (text == nullptr || text[0] != '#') {
        return false;
    }
    uint32_t value = 0;
    for (int i = 1; i <= 6; ++i) {
        if (!isHex(text[i])) {
            return false;
        }
        value = (value << 4) | static_cast<uint32_t>(hexValue(text[i]));
    }
    if (text[7] != '\0') {
        return false;
    }
    rgb = value;
    return true;
}

model::LedSource ledSourceFromText(const char* src, const char* ownId) {
    if (src == nullptr || src[0] == '\0') {
        return model::LedSource::Unknown;
    }
    if (ownId != nullptr && std::strcmp(src, ownId) == 0) return model::LedSource::Self;
    if (std::strcmp(src, "web") == 0) return model::LedSource::Web;
    if (std::strcmp(src, "matter") == 0) return model::LedSource::Matter;
    if (std::strcmp(src, "schedule") == 0) return model::LedSource::Schedule;
    if (std::strcmp(src, "device") == 0) return model::LedSource::Device;
    if (std::strncmp(src, "qnob-", 5) == 0) return model::LedSource::OtherController;
    return model::LedSource::Unknown;
}

bool decodeLedInfo(const char* json, LedInfoMsg& out) {
    out = LedInfoMsg{};
    if (json == nullptr || *skipWs(json) != '{') {
        return false;
    }
    if (!readFixed(findValue(json, "id"), out.id)) {
        return false;
    }
    readFixed(findValue(json, "name"), out.name);
    readFixed(findValue(json, "model"), out.modelName);
    readFixed(findValue(json, "fw"), out.fw);
    readFixed(findValue(json, "ip"), out.ip);
    int32_t n = 0;
    if (readInt(findValue(json, "max_leds"), n) && n >= 0) {
        out.maxLeds = n;
    }
    return true;
}

bool decodeLedStatus(const char* payload, bool& online) {
    if (payload == nullptr) {
        return false;
    }
    const char* p = skipWs(payload);
    char word[16];
    if (*p == '"') {
        if (readString(p, word, sizeof(word)) < 0) {
            return false;
        }
        p = word;
    }
    if (std::strncmp(p, "online", 6) == 0) { online = true; return true; }
    if (std::strncmp(p, "offline", 7) == 0) { online = false; return true; }
    return false;
}

bool decodeLedState(const char* json, const char* ownId, LedStateMsg& out) {
    out = LedStateMsg{};
    if (json == nullptr || *skipWs(json) != '{') {
        return false;
    }
    bool flag = false;
    int32_t n = 0;
    char text[24];
    uint32_t rgb = 0;
    if (readBool(findValue(json, "power"), flag)) { out.state.power = flag; out.fields |= LedStatePower; }
    if (readInt(findValue(json, "brightness"), n) && n >= 0 && n <= model::LightModel::kWireMax) {
        out.state.brightness = static_cast<model::Percent>(model::LightModel::percentFromWire(n));
        out.fields |= LedStateBrightness;
    }
    if (readString(findValue(json, "color"), text, sizeof(text)) >= 0 && parseHexColor(text, rgb)) {
        out.state.rgb = rgb;
        out.fields |= LedStateColor;
    }
    model::LedEffect effect = model::LedEffect::Solid;
    if (readString(findValue(json, "effect"), text, sizeof(text)) >= 0 && model::ledEffectFromName(text, effect)) {
        out.state.effect = effect;
        out.fields |= LedStateEffect;
    }
    int32_t params[model::kLedParamCount] = {0, 0, 0, 0, 0};
    size_t count = 0;
    if (readIntArray(findValue(json, "effect_params"), params, model::kLedParamCount, count) && count >= model::kLedParamCount) {
        for (size_t i = 0; i < model::kLedParamCount; ++i) {
            const int32_t v = params[i] < 0 ? 0 : (params[i] > 255 ? 255 : params[i]);
            out.state.params[i] = static_cast<uint8_t>(v);
        }
        out.fields |= LedStateParams;
    }
    if (readString(findValue(json, "effect_color"), text, sizeof(text)) >= 0 && parseHexColor(text, rgb)) {
        out.state.effectRgb = rgb;
        out.fields |= LedStateEffectColor;
    }
    if (readInt(findValue(json, "count"), n) && n >= 0 && n <= 65535) {
        out.state.count = static_cast<uint16_t>(n);
        out.fields |= LedStateCount;
    }
    if (readString(findValue(json, "src"), text, sizeof(text)) >= 0) {
        out.source = ledSourceFromText(text, ownId);
        out.fields |= LedStateSrc;
    }
    if (readInt(findValue(json, "seq"), n) && n >= 0) {
        out.seq = static_cast<uint32_t>(n);
        out.fields |= LedStateSeq;
    }
    if (readBool(findValue(json, "rejected"), flag)) {
        out.rejected = flag;
    }
    return true;
}

bool decodeLedControllers(const char* json, const char* ownId, LedControllersMsg& out) {
    out = LedControllersMsg{};
    const char* array = findValue(json, "paired");
    if (array == nullptr || *array != '[') {
        return false;
    }
    const char* p = skipWs(array + 1);
    while (*p != ']') {
        if (*p != '{') {
            return false;
        }
        model::FixedString<model::kLedIdCapacity> id;
        model::FixedString<model::kLedNameCapacity> name;
        if (readFixed(findValue(p, "id"), id)) {
            readFixed(findValue(p, "name"), name);
            if (out.count < LedControllersMsg::kMax) {
                out.ids[out.count] = id;
                out.names[out.count] = name;
            }
            ++out.count;
            if (ownId != nullptr && std::strcmp(id.c_str(), ownId) == 0) {
                out.listsOwnId = true;
            }
        }
        p = skipValue(p);
        if (p == nullptr) {
            return false;
        }
        p = skipWs(p);
        if (*p == ',') {
            p = skipWs(p + 1);
        }
    }
    if (out.count > LedControllersMsg::kMax) {
        out.count = LedControllersMsg::kMax;
    }
    return true;
}

int encodeLedSet(char* out, size_t capacity, const model::LedStripState& s, uint16_t fields, const char* from,
                 uint32_t seq) {
    if (out == nullptr || capacity == 0 || from == nullptr || (fields & 0x3Fu) == 0) {
        return -1;
    }
    int n = 0;
    append(out, capacity, &n, "{");
    if (fields & LedStatePower) {
        append(out, capacity, &n, "\"power\":%s,", s.power ? "true" : "false");
    }
    if (fields & LedStateBrightness) {
        append(out, capacity, &n, "\"brightness\":%d,", model::LightModel::wireFromPercent(s.brightness));
    }
    if (fields & LedStateColor) {
        append(out, capacity, &n, "\"color\":\"#%06lx\",", static_cast<unsigned long>(s.rgb & 0xFFFFFFu));
    }
    if (fields & LedStateEffect) {
        append(out, capacity, &n, "\"effect\":\"%s\",", model::ledEffectName(s.effect));
    }
    if (fields & LedStateParams) {
        append(out, capacity, &n, "\"effect_params\":[%u,%u,%u,%u,%u],", s.params[0], s.params[1], s.params[2],
               s.params[3], s.params[4]);
    }
    if (fields & LedStateEffectColor) {
        append(out, capacity, &n, "\"effect_color\":\"#%06lx\",", static_cast<unsigned long>(s.effectRgb & 0xFFFFFFu));
    }
    append(out, capacity, &n, "\"from\":");
    appendQuoted(out, capacity, &n, from);
    append(out, capacity, &n, ",\"seq\":%lu}", static_cast<unsigned long>(seq));
    return n;
}

int encodeLedPair(char* out, size_t capacity, const char* controller, const char* label) {
    if (out == nullptr || capacity == 0 || controller == nullptr) {
        return -1;
    }
    int n = 0;
    append(out, capacity, &n, "{\"controller\":");
    appendQuoted(out, capacity, &n, controller);
    append(out, capacity, &n, ",\"name\":");
    appendQuoted(out, capacity, &n, label != nullptr ? label : "QNOB");
    append(out, capacity, &n, "}");
    return n;
}

int encodeLedPairConfirm(char* out, size_t capacity, const char* controller, int code) {
    if (out == nullptr || capacity == 0 || controller == nullptr || code < 1 || code > 6) {
        return -1;
    }
    int n = 0;
    append(out, capacity, &n, "{\"controller\":");
    appendQuoted(out, capacity, &n, controller);
    append(out, capacity, &n, ",\"code\":%d}", code);
    return n;
}

int encodeLedUnpair(char* out, size_t capacity, const char* controller) {
    if (out == nullptr || capacity == 0 || controller == nullptr) {
        return -1;
    }
    int n = 0;
    append(out, capacity, &n, "{\"controller\":");
    appendQuoted(out, capacity, &n, controller);
    append(out, capacity, &n, "}");
    return n;
}

}  // namespace adapters
