#include "KnobController.h"

#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "hal_config.h"

static const char* TAG = "KnobController";

namespace {
uint32_t nowMs32() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}
}  // namespace

KnobController::KnobController(int tx_pin, int rx_pin, uart_port_t uart_num)
    : uart_num(uart_num), tx_pin(tx_pin), rx_pin(rx_pin), baud_rate(9600), initialized(false),
      assembler(), pendingDelta(0), has_new_message(false), last_action_time(0), message_lock(), last_message() {
    portMUX_INITIALIZE(&message_lock);
}

KnobController::~KnobController() {
    if (initialized) {
        uart_driver_delete(uart_num);
    }
}

esp_err_t KnobController::begin(int baud_rate) {
    this->baud_rate = baud_rate;

    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
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

bool KnobController::getHasNewMessage() const {
    return has_new_message;
}

void KnobController::askSetpoint() {
    sendCommand("sp?");
}

void KnobController::update() {
    if (!initialized) {
        return;
    }

    has_new_message = false;
    const uint32_t now = nowMs32();

    // PCKNOB-09: bounded chunks straight into the assembler; the old code read the whole ring
    // (512 B) into a 256 B stack array and kept only the first line of it.
    size_t consumed = 0;
    while (consumed < kMaxBytesPerUpdate) {
        size_t available = 0;
        if (uart_get_buffered_data_len(uart_num, &available) != ESP_OK || available == 0) {
            break;
        }
        uint8_t chunk[kChunkBytes];
        const size_t want = available < kChunkBytes ? available : kChunkBytes;
        const int len = uart_read_bytes(uart_num, chunk, want, 0);
        if (len <= 0) {
            break;
        }
        assembler.feed(chunk, static_cast<size_t>(len), now);
        consumed += static_cast<size_t>(len);
        last_action_time = now;
    }
    assembler.tick(now);

    KnobLineAssembler::Token token;
    while (assembler.next(token)) {
        if (token.kind == KnobLineAssembler::Token::Kind::Delta) {
            // Saturate far below the int range; the router clears it every frame.
            int expected = pendingDelta.load();
            int next;
            do {
                next = expected + token.delta;
                if (next > 1000) next = 1000;
                if (next < -1000) next = -1000;
            } while (!pendingDelta.compare_exchange_weak(expected, next));
            ESP_LOGD(TAG, "Knob delta %d", token.delta);
        } else {
            portENTER_CRITICAL(&message_lock);
            std::strncpy(last_message, token.text, sizeof(last_message) - 1);
            last_message[sizeof(last_message) - 1] = 0;
            portEXIT_CRITICAL(&message_lock);
            has_new_message = true;
            ESP_LOGD(TAG, "Received from knob: %s", token.text);
        }
    }
}

int KnobController::consumeDelta() {
    return pendingDelta.exchange(0);
}

std::string KnobController::getReceivedCommand() const {
    char copy[KnobLineAssembler::kMaxLine];
    portENTER_CRITICAL(&message_lock);
    std::memcpy(copy, last_message, sizeof(copy));
    portEXIT_CRITICAL(&message_lock);
    copy[sizeof(copy) - 1] = 0;
    return std::string(copy);
}

void KnobController::sendCommand(const std::string& command) {
    sendCommand(command.c_str());
}

void KnobController::sendCommand(const char* command) {
    if (!initialized) {
        ESP_LOGW(TAG, "UART not initialized, cannot send command");
        return;
    }

    const size_t len = std::strlen(command);
    const int written = uart_write_bytes(uart_num, command, len);

    if (written < 0) {
        ESP_LOGE(TAG, "Failed to send command: %s", command);
    } else {
        ESP_LOGD(TAG, "Sent to knob: %s", command);
    }
}
