#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum field sizes (stack-allocated inside QnobEnvelope).
#define QNOB_ENV_CMD_MAX     64
#define QNOB_ENV_AUTH_MAX    48
#define QNOB_ENV_PARAMS_MAX  512
#define QNOB_ENV_LEGACY_MAX  512

// Parsed inbound envelope (JSON or legacy "cmd:param").
typedef struct {
    uint32_t id;
    char     cmd[QNOB_ENV_CMD_MAX];
    char     auth[QNOB_ENV_AUTH_MAX];
    char     params_json[QNOB_ENV_PARAMS_MAX]; // raw JSON object string, or "" if absent
    bool     is_legacy;                         // true when decoded from "cmd:param" format
    char     legacy_param[QNOB_ENV_LEGACY_MAX]; // raw param string when is_legacy == true
} QnobEnvelope;

// Decode a null-terminated input string into an envelope.
// Accepts:
//   - JSON:   {"id":N,"cmd":"name","params":{...},"auth":"token"}
//   - Legacy: "cmdName:param1:param2"
// Returns false if input is neither valid JSON envelope nor legacy format.
bool qnob_envelope_decode(const char* input, QnobEnvelope* out);

// Encode a success response into buf. data_json may be NULL or "" for no data payload.
// Returns bytes written (excluding null). Returns 0 if buf is too small.
size_t qnob_envelope_encode_ok(uint32_t id, const char* data_json, char* buf, size_t cap);

// Encode an error response into buf. detail may be NULL.
// Returns bytes written (excluding null). Returns 0 if buf is too small.
size_t qnob_envelope_encode_err(uint32_t id, const char* error_code, const char* detail,
                                 char* buf, size_t cap);

// Encode a ping/status response: {"id":N,"status":"ok","data":{"uptime_ms":U}}.
size_t qnob_envelope_encode_ping(uint32_t id, uint32_t uptime_ms, char* buf, size_t cap);

#ifdef __cplusplus
}
#endif
