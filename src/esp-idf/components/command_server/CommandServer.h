#pragma once

#include <atomic>

class CommandHandler;

// TCP command server — listens on port 23456, accepts up to 2 clients,
// speaks the Qnob newline-delimited JSON envelope protocol (docs/protocol.md).
// Start on STA-got-IP, stop on STA-lost.
class CommandServer {
public:
    CommandServer();
    ~CommandServer();

    void start(CommandHandler* handler);
    void stop();

    bool isRunning() const { return running_.load(); }

private:
    static constexpr int    kPort           = 23456;
    static constexpr int    kMaxClients     = 2;
    static constexpr size_t kMaxLineBytes   = 8192;   // max frame size per client buffer
    static constexpr size_t kClientStack    = 8192;
    static constexpr size_t kAcceptStack    = 4096;

    std::atomic<bool>   running_;
    std::atomic<int>    active_clients_;
    int                 server_fd_;
    CommandHandler*     handler_;

    static void acceptTask(void* arg);
    static void clientTaskEntry(void* arg); // arg = heap-allocated ClientCtx*

    void acceptLoop();
    void clientLoop(int fd);
    void processLine(int fd, char* line, size_t len);
    void sendLine(int fd, const char* buf, size_t len);
};
