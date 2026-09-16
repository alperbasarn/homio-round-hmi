#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "KnobLineAssembler.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

// The knob link (PCKNOB-09 / PCKNOB-11). update() runs on the display task (InputRouter::poll),
// reads the UART in bounded chunks into the line assembler, and sorts the tokens: detents are
// summed into an atomic that consumeDelta() reads and clears; every other line is the last
// message, kept in a fixed buffer behind a spinlock so the command task can copy it safely.
class KnobController {
public:
    KnobController(int tx_pin, int rx_pin, uart_port_t uart_num = UART_NUM_1);
    ~KnobController();

    esp_err_t begin(int baud_rate = 9600);
    void update();
    // PCKNOB-15: push the screen's value to the knob as one "setpoint=N" line; the knob acks with
    // "ok:setpoint=N" (the router turns it into a KnobAck event and the model clears the write).
    void setSetpoint(int value);

    // Signed detents accumulated since the last call (each token clamped to +-10, D5); clears.
    int consumeDelta();

    // The last non-delta line (a setpoint burst, a reply, garbage). hasNewMessage() is true for
    // the update() that received it, as before.
    std::string getReceivedCommand() const;
    bool getHasNewMessage() const;

    void sendCommand(const std::string& command);
    void sendCommand(const char* command);

    uint32_t droppedTokens() const { return assembler.overflow(); }
    uint32_t truncatedLines() const { return assembler.truncated(); }

private:
    uart_port_t uart_num;
    int tx_pin;
    int rx_pin;
    int baud_rate;
    bool initialized;

    KnobLineAssembler assembler;
    std::atomic<int> pendingDelta;
    bool has_new_message;
    int64_t last_action_time;
    mutable portMUX_TYPE message_lock;
    char last_message[KnobLineAssembler::kMaxLine];

    static const int RX_BUF_SIZE = 256;
    static const int TX_BUF_SIZE = 256;
    static constexpr size_t kChunkBytes = 64;
    static constexpr size_t kMaxBytesPerUpdate = 512;
};
