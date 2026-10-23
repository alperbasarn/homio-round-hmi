#include "CommandEnvelope.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <ctype.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "CmdEnv";

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Safely copy at most (dst_size - 1) bytes and null-terminate.
static void safe_copy(char* dst, size_t dst_size, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

// Write escaped JSON string content (without surrounding quotes) into buf.
// Returns bytes written, or needed bytes if buf==NULL / cap==0.
static size_t json_escape(const char* s, char* buf, size_t cap) {
    size_t n = 0;
    for (; *s; ++s) {
        char c = *s;
        const char* esc = nullptr;
        char tmp[3];
        if      (c == '"')  esc = "\\\"";
        else if (c == '\\') esc = "\\\\";
        else if (c == '\n') esc = "\\n";
        else if (c == '\r') esc = "\\r";
        else if (c == '\t') esc = "\\t";
        else { tmp[0] = c; tmp[1] = '\0'; esc = tmp; }
        size_t len = strlen(esc);
        if (buf && n + len < cap) memcpy(buf + n, esc, len);
        n += len;
    }
    if (buf && n < cap) buf[n] = '\0';
    return n;
}

// ---------------------------------------------------------------------------
// Decode
// ---------------------------------------------------------------------------

bool qnob_envelope_decode(const char* input, QnobEnvelope* out) {
    if (!input || !out) return false;

    memset(out, 0, sizeof(*out));

    // Try JSON first.
    cJSON* root = cJSON_Parse(input);
    if (root) {
        bool valid = false;

        const cJSON* j_cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
        if (cJSON_IsString(j_cmd) && j_cmd->valuestring && j_cmd->valuestring[0]) {
            safe_copy(out->cmd, sizeof(out->cmd), j_cmd->valuestring);
            valid = true;
        }

        if (valid) {
            const cJSON* j_id = cJSON_GetObjectItemCaseSensitive(root, "id");
            if (cJSON_IsNumber(j_id)) {
                out->id = static_cast<uint32_t>(j_id->valuedouble);
            }

            const cJSON* j_auth = cJSON_GetObjectItemCaseSensitive(root, "auth");
            if (cJSON_IsString(j_auth) && j_auth->valuestring) {
                safe_copy(out->auth, sizeof(out->auth), j_auth->valuestring);
            }

            const cJSON* j_params = cJSON_GetObjectItemCaseSensitive(root, "params");
            if (j_params && !cJSON_IsNull(j_params)) {
                char* rendered = cJSON_PrintUnformatted(j_params);
                if (rendered) {
                    safe_copy(out->params_json, sizeof(out->params_json), rendered);
                    cJSON_free(rendered);
                }
            }
        }

        cJSON_Delete(root);

        if (valid) {
            return true;
        }
        // JSON parsed but no "cmd" field — fall through to legacy.
    }

    // Legacy "cmd:param" format.
    const char* colon = strchr(input, ':');
    if (colon) {
        size_t cmd_len = static_cast<size_t>(colon - input);
        if (cmd_len > 0 && cmd_len < QNOB_ENV_CMD_MAX) {
            memcpy(out->cmd, input, cmd_len);
            out->cmd[cmd_len] = '\0';
            safe_copy(out->legacy_param, sizeof(out->legacy_param), colon + 1);
            out->is_legacy = true;
            out->id = 0;
            return true;
        }
    }

    // Bare command name with no params.
    size_t len = strlen(input);
    if (len > 0 && len < QNOB_ENV_CMD_MAX) {
        // Reject if it contains whitespace-only or looks like garbage.
        bool alpha = false;
        for (size_t i = 0; i < len; i++) {
            if (isalpha(static_cast<unsigned char>(input[i])) || input[i] == '+' || input[i] == '-') {
                alpha = true;
                break;
            }
        }
        if (alpha) {
            safe_copy(out->cmd, sizeof(out->cmd), input);
            out->is_legacy = true;
            return true;
        }
    }

    ESP_LOGW(TAG, "Envelope decode failed for input: %.40s", input);
    return false;
}

// ---------------------------------------------------------------------------
// Encode helpers
// ---------------------------------------------------------------------------

size_t qnob_envelope_encode_ok(uint32_t id, const char* data_json, char* buf, size_t cap) {
    if (!data_json || data_json[0] == '\0') {
        int n = snprintf(buf, cap, "{\"id\":%" PRIu32 ",\"status\":\"ok\"}", id);
        return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
    }
    int n = snprintf(buf, cap, "{\"id\":%" PRIu32 ",\"status\":\"ok\",\"data\":%s}", id, data_json);
    return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}

size_t qnob_envelope_encode_err(uint32_t id, const char* error_code, const char* detail,
                                 char* buf, size_t cap) {
    const char* code = (error_code && error_code[0]) ? error_code : "internal";

    if (!detail || detail[0] == '\0') {
        int n = snprintf(buf, cap,
                         "{\"id\":%" PRIu32 ",\"status\":\"err\",\"error\":\"%s\"}",
                         id, code);
        return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
    }

    // Build with escaped detail.
    size_t needed = snprintf(nullptr, 0,
                             "{\"id\":%" PRIu32 ",\"status\":\"err\",\"error\":\"%s\",\"detail\":\"",
                             id, code);
    needed += json_escape(detail, nullptr, 0);
    needed += 2; // closing "}

    if (cap == 0 || needed + 1 > cap) return 0;

    size_t pos = static_cast<size_t>(
        snprintf(buf, cap,
                 "{\"id\":%" PRIu32 ",\"status\":\"err\",\"error\":\"%s\",\"detail\":\"",
                 id, code));
    pos += json_escape(detail, buf + pos, cap - pos);
    if (pos + 2 < cap) {
        buf[pos++] = '"';
        buf[pos++] = '}';
        buf[pos]   = '\0';
    }
    return pos;
}

size_t qnob_envelope_encode_ping(uint32_t id, uint32_t uptime_ms, char* buf, size_t cap) {
    int n = snprintf(buf, cap,
                     "{\"id\":%" PRIu32 ",\"status\":\"ok\",\"data\":{\"uptime_ms\":%" PRIu32 "}}",
                     id, uptime_ms);
    return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}
