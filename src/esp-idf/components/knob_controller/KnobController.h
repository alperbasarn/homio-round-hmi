#pragma once

#include <string>
#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus

class KnobController {
public:
    KnobController(int tx_pin, int rx_pin, uart_port_t uart_num = UART_NUM_1);
    ~KnobController();

    esp_err_t begin(int baud_rate = 9600);
    void update();
    void askSetpoint();
    std::string getReceivedCommand() const;
    bool getHasNewMessage() const;
    void sendCommand(const std::string& command);
    void sendCommand(const char* command);

private:
    uart_port_t uart_num;
    int tx_pin;
    int rx_pin;
    int baud_rate;
    bool initialized;

    std::string command_from_knob;
    int64_t last_action_time;
    bool has_new_message;

    static const int RX_BUF_SIZE = 256;
    static const int TX_BUF_SIZE = 256;
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

// C-compatible interface for use from C code if needed
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
