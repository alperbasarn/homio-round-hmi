#include "KnobController.h"
#include "hal_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "KnobController";

KnobController::KnobController(int tx_pin, int rx_pin, uart_port_t uart_num)
    : uart_num(uart_num), tx_pin(tx_pin), rx_pin(rx_pin), baud_rate(9600),
      initialized(false), command_from_knob(""), last_action_time(0), has_new_message(false)
{
}

KnobController::~KnobController()
{
    if (initialized) {
        uart_driver_delete(uart_num);
    }
}

esp_err_t KnobController::begin(int baud_rate)
{
    this->baud_rate = baud_rate;

    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(uart_num, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART parameters: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_driver_install(uart_num, RX_BUF_SIZE * 2, TX_BUF_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }

    initialized = true;
    ESP_LOGI(TAG, "UART initialized on TX=%d, RX=%d @ %d baud", tx_pin, rx_pin, baud_rate);
    return ESP_OK;
}

bool KnobController::getHasNewMessage() const
{
    return has_new_message;
}

void KnobController::askSetpoint()
{
    sendCommand("sp?");
}

void KnobController::update()
{
    if (!initialized) {
        return;
    }

    has_new_message = false;

    size_t available = 0;
    uart_get_buffered_data_len(uart_num, &available);

    if (available > 0) {
        has_new_message = true;
        command_from_knob.clear();
        last_action_time = esp_timer_get_time() / 1000;

        uint8_t data[RX_BUF_SIZE];
        int len = uart_read_bytes(uart_num, data, available, pdMS_TO_TICKS(100));

        if (len > 0) {
            for (int i = 0; i < len; i++) {
                if (data[i] == '\n' || data[i] == '\r') {
                    break;
                }
                command_from_knob += static_cast<char>(data[i]);
            }

            if (!command_from_knob.empty()) {
                ESP_LOGD(TAG, "Received from knob: %s", command_from_knob.c_str());
            }
        }
    }
}

std::string KnobController::getReceivedCommand() const
{
    return command_from_knob;
}

void KnobController::sendCommand(const std::string& command)
{
    sendCommand(command.c_str());
}

void KnobController::sendCommand(const char* command)
{
    if (!initialized) {
        ESP_LOGW(TAG, "UART not initialized, cannot send command");
        return;
    }

    size_t len = strlen(command);
    int written = uart_write_bytes(uart_num, command, len);

    if (written < 0) {
        ESP_LOGE(TAG, "Failed to send command: %s", command);
    } else {
        ESP_LOGD(TAG, "Sent to knob: %s", command);
    }
}

// C-compatible interface implementation
extern "C" {

knob_controller_handle_t knob_controller_create(int tx_pin, int rx_pin)
{
    return new KnobController(tx_pin, rx_pin);
}

void knob_controller_destroy(knob_controller_handle_t handle)
{
    delete static_cast<KnobController*>(handle);
}

esp_err_t knob_controller_begin(knob_controller_handle_t handle, int baud_rate)
{
    return static_cast<KnobController*>(handle)->begin(baud_rate);
}

void knob_controller_update(knob_controller_handle_t handle)
{
    static_cast<KnobController*>(handle)->update();
}

bool knob_controller_has_new_message(knob_controller_handle_t handle)
{
    return static_cast<KnobController*>(handle)->getHasNewMessage();
}

const char* knob_controller_get_command(knob_controller_handle_t handle)
{
    static std::string cmd;
    cmd = static_cast<KnobController*>(handle)->getReceivedCommand();
    return cmd.c_str();
}

void knob_controller_send_command(knob_controller_handle_t handle, const char* cmd)
{
    static_cast<KnobController*>(handle)->sendCommand(cmd);
}

}
