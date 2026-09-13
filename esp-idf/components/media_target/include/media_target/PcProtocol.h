#pragma once
// PcProtocol (MEDIA-04 / MEDIA-05, folds CTRL-18): the wire grammar between the knob and the PC
// media app, as both sides speak it today (PC/src/pc_media_protocol.py is the other half).
//
//   esp32/sound/setpoint   knob -> PC   setpoint:<0-100>
//   esp32/sound/control    knob -> PC   play | pause | forward | rewind | toggle (v2)
//   esp32/sound/get_state  knob -> PC   request | request:v2 (an old PC answers v1 to both)
//   esp32/sound/response   PC -> knob   playing | paused | unpaused | setpoint:<n> |
//                                       state:<playing|paused>,volume:<n>[,position:<ms>,duration:<ms>,title:<t>,player:<p>]
//
// The MQTT manager wraps every body in "sender=<id>," and strips it before dispatch; parseResponse
// tolerates both. Everything here is pure and fixed-size.
#include <cstddef>
#include <cstdint>

#include "model/ModelTypes.h"

namespace media {
namespace pc {

constexpr const char* kTopicControl = "esp32/sound/control";
constexpr const char* kTopicSetpoint = "esp32/sound/setpoint";
constexpr const char* kTopicResponse = "esp32/sound/response";
constexpr const char* kTopicGetState = "esp32/sound/get_state";
constexpr size_t kBodyCapacity = 96;

enum class Control : uint8_t { Play = 0, Pause, Forward, Rewind, Toggle };

// Encoders return the length written, like snprintf, or -1 when the value is out of range or the
// buffer is too small. The three v1 bodies are byte-identical to what SoundController published.
int encodeSetpoint(char* out, size_t capacity, int percent);
int encodeControl(char* out, size_t capacity, Control verb);
int encodeStateRequest(char* out, size_t capacity, bool v2);
int encodeSeekAbsolute(char* out, size_t capacity, int percent);
int encodeSeekRelative(char* out, size_t capacity, int32_t deltaMs);

struct Response {
    model::FixedString<32> sender;  // from the envelope when present
    bool hasPlayState = false;
    bool playing = false;
    bool hasVolume = false;
    int volume = 0;
    bool hasPosition = false;
    int32_t positionMs = 0;
    bool hasDuration = false;
    int32_t durationMs = 0;
    bool hasTitle = false;
    model::FixedString<64> title;
    bool hasPlayer = false;
    model::FixedString<24> player;
    bool v2 = false;  // position or duration was present: the PC speaks v2
    bool any() const { return hasPlayState || hasVolume || hasPosition || hasDuration || hasTitle || hasPlayer; }
};

// Returns the body after a "sender=<id>," envelope (copying the id) or the payload itself.
const char* stripSender(const char* payload, char* senderOut, size_t senderCapacity);
// False when nothing usable was found (empty payload, garbage, every value out of range). A bad
// value never sets its field: "volume:200" leaves hasVolume false and keeps the play state.
bool parseResponse(const char* payload, Response& out);

}  // namespace pc
}  // namespace media
