#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "KnobLineAssembler.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus

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
    void askSetpoint();

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

#endif

#ifdef __cplusplus
extern "C" {
#endif

// C-compatible interface for use from C code if needed (INPT-27 deletes it)
typedef void* knob_controller_handle_t;

knob_controller_handle_t knob_controller_create(int tx_pin, int rx_pin);
void knob_controller_destroy(knob_controller_handle_t handle);
esp_err_t knob_controller_begin(knob_controller_handle_t handle, int baud_rate);
void knob_controller_update(knob_controller_handle_t handle);
bool knob_controller_has_new_message(knob_controller_handle_t handle);
const char* knob_controller_get_command(knob_controller_handle_t handle);
void knob_controller_send_command(knob_controller_handle_t handle, const char* cmd);

#ifdef __cplusplus
}
#endif
