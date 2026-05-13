#include "CommandServer.h"
#include "CommandHandler.h"
#include "CommandEnvelope.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char* TAG = "CommandServer";

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

CommandServer::CommandServer()
    : running_(false), active_clients_(0), server_fd_(-1), handler_(nullptr) {}

CommandServer::~CommandServer() {
    stop();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void CommandServer::start(CommandHandler* handler) {
    if (running_.load()) {
        return;
    }
    handler_ = handler;
    running_.store(true);
    xTaskCreate(acceptTask, "CmdSrvAccept", kAcceptStack, this, 5, nullptr);
    ESP_LOGI(TAG, "TCP command server starting on port %d", kPort);
}

void CommandServer::stop() {
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }
    // Per-client tasks detect running_==false on next recv timeout and exit.
    ESP_LOGI(TAG, "TCP command server stopping");
}

// ---------------------------------------------------------------------------
// Accept loop task
// ---------------------------------------------------------------------------

void CommandServer::acceptTask(void* arg) {
    static_cast<CommandServer*>(arg)->acceptLoop();
    vTaskDelete(nullptr);
}

struct ClientCtx {
    CommandServer* server;
    int            fd;
};

void CommandServer::clientTaskEntry(void* arg) {
    ClientCtx* ctx = static_cast<ClientCtx*>(arg);
    CommandServer* srv = ctx->server;
    int fd = ctx->fd;
    delete ctx;
    srv->clientLoop(fd);
    vTaskDelete(nullptr);
}

void CommandServer::acceptLoop() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd_ < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        running_.store(false);
        return;
    }

    int reuse = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(kPort);

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind() failed: %d", errno);
        close(server_fd_);
        server_fd_ = -1;
        running_.store(false);
        return;
    }

    if (listen(server_fd_, kMaxClients) != 0) {
        ESP_LOGE(TAG, "listen() failed: %d", errno);
        close(server_fd_);
        server_fd_ = -1;
        running_.store(false);
        return;
    }

    // 1 s accept timeout so we can re-check running_ when idle.
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(server_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "Listening on :%d (max %d clients)", kPort, kMaxClients);

    while (running_.load()) {
        struct sockaddr_in client_addr = {};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            if (running_.load()) {
                ESP_LOGW(TAG, "accept() error: %d", errno);
            }
            continue;
        }

        if (active_clients_.load() >= kMaxClients) {
            char busy[96];
            qnob_envelope_encode_err(0, "busy", "max clients reached", busy, sizeof(busy));
            send(client_fd, busy, strlen(busy), 0);
            send(client_fd, "\n", 1, 0);
            close(client_fd);
            ESP_LOGW(TAG, "Rejected: max clients active");
            continue;
        }

        // 60 s idle timeout per client socket.
        struct timeval ctv = { .tv_sec = 60, .tv_usec = 0 };
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &ctv, sizeof(ctv));

        active_clients_.fetch_add(1);
        ESP_LOGI(TAG, "Client connected (fd=%d active=%d)", client_fd, active_clients_.load());

        ClientCtx* ctx = new ClientCtx{ this, client_fd };
        if (xTaskCreate(clientTaskEntry, "CmdSrvClient", kClientStack, ctx, 5, nullptr) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create client task");
            delete ctx;
            close(client_fd);
            active_clients_.fetch_sub(1);
        }
    }

    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    ESP_LOGI(TAG, "Accept loop exited");
}

// ---------------------------------------------------------------------------
// Per-client loop
// ---------------------------------------------------------------------------

void CommandServer::clientLoop(int fd) {
    char* buf = static_cast<char*>(malloc(kMaxLineBytes));
    if (!buf) {
        ESP_LOGE(TAG, "OOM for client buffer (fd=%d)", fd);
        close(fd);
        active_clients_.fetch_sub(1);
        return;
    }

    size_t pos = 0;

    while (running_.load()) {
        int n = recv(fd, buf + pos, kMaxLineBytes - pos - 1, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue; // idle timeout, check running_
            }
            break;
        }

        pos += static_cast<size_t>(n);
        buf[pos] = '\0';

        char* search = buf;
        while (true) {
            char* nl = static_cast<char*>(memchr(search, '\n', static_cast<size_t>(buf + pos - search)));
            if (!nl) break;

            *nl = '\0';
            size_t line_len = static_cast<size_t>(nl - search);
            if (line_len > 0 && search[line_len - 1] == '\r') {
                search[--line_len] = '\0';
            }
            if (line_len > 0) {
                processLine(fd, search, line_len);
            }
            search = nl + 1;
        }

        size_t consumed = static_cast<size_t>(search - buf);
        pos -= consumed;
        if (consumed > 0 && pos > 0) {
            memmove(buf, search, pos);
        }

        if (pos >= kMaxLineBytes - 1) {
            char err[96];
            qnob_envelope_encode_err(0, "frame_too_large", nullptr, err, sizeof(err));
            sendLine(fd, err, strlen(err));
            pos = 0;
        }
    }

    free(buf);
    close(fd);
    int rem = active_clients_.fetch_sub(1) - 1;
    ESP_LOGI(TAG, "Client disconnected (fd=%d remaining=%d)", fd, rem);
}

void CommandServer::sendLine(int fd, const char* data, size_t len) {
    send(fd, data, len, MSG_MORE);
    send(fd, "\n", 1, 0);
}

// ---------------------------------------------------------------------------
// Per-line dispatch
// ---------------------------------------------------------------------------

void CommandServer::processLine(int fd, char* line, size_t /*len*/) {
    QnobEnvelope env = {};
    if (!qnob_envelope_decode(line, &env)) {
        char err[96];
        qnob_envelope_encode_err(0, "bad_frame", "parse error", err, sizeof(err));
        sendLine(fd, err, strlen(err));
        return;
    }

    // ping — handled internally.
    if (strcmp(env.cmd, "ping") == 0) {
        char resp[128];
        uint32_t uptime_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        qnob_envelope_encode_ping(env.id, uptime_ms, resp, sizeof(resp));
        sendLine(fd, resp, strlen(resp));
        return;
    }

    if (!handler_) {
        char err[96];
        qnob_envelope_encode_err(env.id, "internal", "no handler", err, sizeof(err));
        sendLine(fd, err, strlen(err));
        return;
    }

    // Build legacy "cmd[:param]" string for CommandHandler.
    // Migration period: JSON params string passed verbatim as the param segment;
    // individual command handlers will be updated to parse their params as needed.
    std::string cmd_str(env.cmd);
    if (env.is_legacy) {
        if (env.legacy_param[0]) {
            cmd_str += ':';
            cmd_str += env.legacy_param;
        }
    } else if (env.params_json[0] && strcmp(env.params_json, "{}") != 0) {
        cmd_str += ':';
        cmd_str += env.params_json;
    }

    std::string legacy_resp;
    handler_->handleExternalCommand(cmd_str, legacy_resp);

    // Map legacy "cmd:OK[:detail]" / "cmd:ERR:code" response to envelope JSON.
    char resp_buf[512];
    if (legacy_resp.empty()) {
        qnob_envelope_encode_ok(env.id, nullptr, resp_buf, sizeof(resp_buf));
        sendLine(fd, resp_buf, strlen(resp_buf));
        return;
    }

    const char* p1 = strchr(legacy_resp.c_str(), ':');
    if (!p1) {
        // No colon — treat whole string as result.
        char data[288];
        snprintf(data, sizeof(data), "{\"result\":\"%s\"}", legacy_resp.c_str());
        qnob_envelope_encode_ok(env.id, data, resp_buf, sizeof(resp_buf));
        sendLine(fd, resp_buf, strlen(resp_buf));
        return;
    }

    const char* status = p1 + 1;
    const char* p2     = strchr(status, ':');
    size_t      slen   = p2 ? static_cast<size_t>(p2 - status) : strlen(status);

    if (slen == 2 && strncmp(status, "OK", 2) == 0) {
        if (p2 && p2[1]) {
            char data[288];
            snprintf(data, sizeof(data), "{\"result\":\"%s\"}", p2 + 1);
            qnob_envelope_encode_ok(env.id, data, resp_buf, sizeof(resp_buf));
        } else {
            qnob_envelope_encode_ok(env.id, nullptr, resp_buf, sizeof(resp_buf));
        }
    } else if (slen == 3 && strncmp(status, "ERR", 3) == 0) {
        const char* code = (p2 && p2[1]) ? p2 + 1 : "error";
        qnob_envelope_encode_err(env.id, code, nullptr, resp_buf, sizeof(resp_buf));
    } else {
        // Non-standard format — echo as result.
        char data[288];
        snprintf(data, sizeof(data), "{\"result\":\"%s\"}", legacy_resp.c_str());
        qnob_envelope_encode_ok(env.id, data, resp_buf, sizeof(resp_buf));
    }

    sendLine(fd, resp_buf, strlen(resp_buf));
}
