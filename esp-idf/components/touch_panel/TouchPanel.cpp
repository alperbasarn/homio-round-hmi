#include "TouchPanel.h"
#include "hal_config.h"
#include "hal_init.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdlib>

static const char *TAG = "TouchPanel";

TouchPanel::TouchPanel(int sda, int scl, int rst, int irq)
    : sda_pin(sda), scl_pin(scl), rst_pin(rst), irq_pin(irq),
      i2c_addr(TP_I2C_ADDR), initialized(false),
      first_press(true), first_release(true),
      pressed(false), released(true), hold(false), long_hold(false),
      has_new_press(false), has_new_release(false), has_new_hold_release(false),
      has_new_gesture(false), last_gesture(GESTURE_NONE),
      tap_confirmed(false), swipe_candidate(false), max_move_sq(0),
      press_time(0), release_time(0), last_press_duration_ms(0),
      hold_threshold_ms(1000), long_hold_threshold_ms(10000), release_threshold_ms(30),
      touch_x(0), touch_y(0), press_x(0), press_y(0), release_x(0), release_y(0),
      gesture(GESTURE_NONE), i2c_dev_handle(nullptr)
{
}

TouchPanel::~TouchPanel()
{
    if (i2c_dev_handle != nullptr) {
        i2c_master_bus_rm_device(i2c_dev_handle);
        i2c_dev_handle = nullptr;
    }
}

void TouchPanel::reset()
{
#if TP_RST_PIN >= 0
    if (rst_pin >= 0) {
        gpio_set_level((gpio_num_t)rst_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)rst_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_LOGI(TAG, "Touch controller reset complete");
    }
#endif
}

esp_err_t TouchPanel::i2c_read_reg(uint8_t reg, uint8_t* data, size_t len)
{
    if (i2c_dev_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(i2c_dev_handle, &reg, 1, data, len, I2C_MASTER_TIMEOUT_MS);
}

esp_err_t TouchPanel::i2c_write_reg(uint8_t reg, uint8_t data)
{
    if (i2c_dev_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t tx[2] = {reg, data};
    return i2c_master_transmit(i2c_dev_handle, tx, sizeof(tx), I2C_MASTER_TIMEOUT_MS);
}

esp_err_t TouchPanel::initialize()
{
    ESP_LOGI(TAG, "TouchPanel::initialize() called");
    ESP_LOGI(TAG, "Initializing touch controller (addr=0x%02X)...", i2c_addr);

    i2c_master_bus_handle_t bus_handle = hal_i2c_get_bus_handle();
    if (bus_handle == nullptr) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (i2c_dev_handle == nullptr) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = i2c_addr,
            .scl_speed_hz = 300000,
            .scl_wait_us = 0,
            .flags = {
                // Disable ACK check to suppress NACK errors during polling.
                // Touch controllers often NACK when no finger is present.
                .disable_ack_check = true,
            },
        };
        esp_err_t add_dev_err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &i2c_dev_handle);
        if (add_dev_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add touch I2C device: %s", esp_err_to_name(add_dev_err));
            return add_dev_err;
        }
    }

    // Reset the touch controller
    reset();

    // Match official board flow: switch touch IC to normal mode.
    esp_err_t mode_err = ESP_FAIL;
    for (int i = 0; i < 4; ++i) {
        mode_err = i2c_write_reg(0x86, 0x00);
        if (mode_err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (mode_err != ESP_OK) {
        ESP_LOGW(TAG, "Touch normal-mode command failed: %s", esp_err_to_name(mode_err));
    }

    // Do not discard the first real user touch event.
    first_press = false;
    first_release = false;
    pressed = false;
    released = true;
    hold = false;
    long_hold = false;
    tap_confirmed = false;
    swipe_candidate = false;
    max_move_sq = 0;

    press_time = esp_timer_get_time() / 1000;
    last_press_duration_ms = 0;
    initialized = true;

    ESP_LOGI(TAG, "Touch initialization complete");
    return ESP_OK;
}

uint8_t TouchPanel::getChipId()
{
    uint8_t chip_id = 0;
    if (i2c_read_reg(CST816S_REG_CHIP_ID, &chip_id, 1) != ESP_OK) {
        return 0;
    }
    return chip_id;
}

uint8_t TouchPanel::getFirmwareVersion()
{
    uint8_t version = 0;
    if (i2c_read_reg(CST816S_REG_FW_VERSION, &version, 1) != ESP_OK) {
        return 0;
    }
    return version;
}

bool TouchPanel::readTouchData(touch_data_t* data)
{
    // Read gesture register first
    uint8_t gesture_id = 0;
    if (i2c_read_reg(CST816S_REG_GESTURE_ID, &gesture_id, 1) == ESP_OK) {
        data->gesture = static_cast<touch_gesture_t>(gesture_id);
    } else {
        data->gesture = GESTURE_NONE;
    }

    uint8_t points = 0;
    if (i2c_read_reg(CST816S_REG_FINGER_NUM, &points, 1) != ESP_OK) {
        data->available = false;
        return false;
    }

    // This board reports one active point when touched.
    if (points != 1) {
        data->available = false;
        return false;
    }

    uint8_t xy[4] = {0};
    if (i2c_read_reg(CST816S_REG_XPOS_H, xy, sizeof(xy)) != ESP_OK) {
        data->available = false;
        return false;
    }

    data->fingers = points;
    data->x = ((xy[0] & 0x0F) << 8) | xy[1];
    data->y = ((xy[2] & 0x0F) << 8) | xy[3];
    if (data->x >= DISPLAY_WIDTH) {
        data->x = DISPLAY_WIDTH - 1;
    }
    if (data->y >= DISPLAY_HEIGHT) {
        data->y = DISPLAY_HEIGHT - 1;
    }
    data->available = true;

    return data->available;
}

void TouchPanel::prepareForSleep()
{
    if (!initialized || i2c_dev_handle == nullptr) {
        return;
    }

    // FT6146 register 0xFE: disable auto-sleep so IC stays responsive
    i2c_write_reg(0xFE, 0x01);

    // FT6146 register 0xA5: set power mode to Monitor (0x01)
    // Monitor mode = low-power periodic scanning, asserts INT on touch
    i2c_write_reg(0xA5, 0x01);

    ESP_LOGI(TAG, "Touch IC configured for sleep wake-on-touch (monitor mode)");
}

void TouchPanel::handleTouchPanel()
{
    if (!initialized) {
        return;
    }

    int64_t current_time = esp_timer_get_time() / 1000;  // Convert to milliseconds

    has_new_press = false;
    has_new_release = false;
    has_new_hold_release = false;
    has_new_gesture = false;

    auto finalizeRelease = [&](int64_t effective_release_time) {
        last_press_duration_ms = effective_release_time - press_time;
        if (last_press_duration_ms < 0) {
            last_press_duration_ms = 0;
        }

        touch_gesture_t detected_gesture = GESTURE_NONE;

        const int delta_x = release_x - press_x;
        const int delta_y = release_y - press_y;
        const int dist_sq = (delta_x * delta_x) + (delta_y * delta_y);
        const int radius_sq = TAP_RELEASE_RADIUS_PX * TAP_RELEASE_RADIUS_PX;
        const int swipe_sq = SWIPE_MIN_PX * SWIPE_MIN_PX;
        const bool is_swipe = swipe_candidate || (dist_sq >= swipe_sq);
        const bool quick_tap = (last_press_duration_ms > 0 &&
                                last_press_duration_ms < TAP_CONFIRM_MS);

        if (is_swipe) {
            if (std::abs(delta_x) >= std::abs(delta_y)) {
                detected_gesture = (delta_x >= 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
            } else {
                detected_gesture = (delta_y >= 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
            }
        } else if (hold) {
            has_new_hold_release = true;
            detected_gesture = GESTURE_HOLD_RELEASE;
        } else if (dist_sq <= radius_sq && (tap_confirmed || quick_tap)) {
            has_new_release = true;
            detected_gesture = GESTURE_TAP_RELEASE;
        }

        if (first_release) {
            first_release = false;
            has_new_release = false;
            has_new_hold_release = false;
            has_new_gesture = false;
            last_gesture = GESTURE_NONE;
            gesture = GESTURE_NONE;
            released = true;
        } else {
            released = true;
            if (detected_gesture != GESTURE_NONE) {
                has_new_gesture = true;
                last_gesture = detected_gesture;
                gesture = detected_gesture;
            }
        }

        long_hold = false;
        hold = false;
        pressed = false;
        release_time = 0;
    };

    touch_data_t data;
    bool touch_available = readTouchData(&data);

    if (touch_available) {
        if (pressed && release_time > 0) {
            if ((current_time - release_time) >= REPRESS_MIN_MS) {
                finalizeRelease(release_time);
            }
            release_time = 0;
        }

        // Clear pending release timer when touch is present again.
        release_time = 0;

        if (!first_press) {
            touch_x = data.x;
            touch_y = data.y;
            if (!pressed) {
                pressed = true;
                released = false;
                press_time = current_time;
                last_press_duration_ms = 0;
                press_x = data.x;
                press_y = data.y;
                release_x = data.x;
                release_y = data.y;
                last_gesture = GESTURE_NONE;
                gesture = GESTURE_NONE;
                tap_confirmed = false;
                swipe_candidate = false;
                max_move_sq = 0;
                has_new_press = true;
            } else {
                const int dx = data.x - press_x;
                const int dy = data.y - press_y;
                const int dist_sq = (dx * dx) + (dy * dy);
                if (dist_sq > max_move_sq) {
                    max_move_sq = dist_sq;
                }
                if (!swipe_candidate && dist_sq >= (SWIPE_MIN_PX * SWIPE_MIN_PX)) {
                    swipe_candidate = true;
                }
                if (!tap_confirmed &&
                    (current_time - press_time >= TAP_CONFIRM_MS) &&
                    max_move_sq <= (TAP_RELEASE_RADIUS_PX * TAP_RELEASE_RADIUS_PX)) {
                    tap_confirmed = true;
                }
                if (swipe_candidate) {
                    tap_confirmed = false;
                }

                if (!hold && (current_time - press_time > hold_threshold_ms)) {
                    if (!long_hold && (current_time - press_time > long_hold_threshold_ms)) {
                        long_hold = true;
                    }
                    hold = true;
                }
            }
            release_x = data.x;
            release_y = data.y;
        } else {
            first_press = false;
        }
    } else {
        if (pressed && release_time == 0) {
            release_time = current_time;
            release_x = touch_x;
            release_y = touch_y;
        }

        if (pressed && !released && release_time > 0 &&
            (current_time - release_time > release_threshold_ms)) {
            finalizeRelease(release_time);
        }
    }
}
