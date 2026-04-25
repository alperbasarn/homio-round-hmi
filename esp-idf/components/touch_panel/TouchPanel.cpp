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

namespace {

constexpr uint8_t FT6X36_POWER_MODE_ACTIVE = 0x00;
constexpr uint8_t FT6X36_POWER_MODE_MONITOR = 0x01;
constexpr uint16_t CST9217_REG_TOUCH_DATA = 0xD000;
constexpr uint16_t CST9217_REG_INFO_MODE = 0xD101;
constexpr uint16_t CST9217_REG_SLEEP_MODE = 0xD105;
constexpr uint16_t CST9217_REG_RESOLUTION = 0xD1F8;
constexpr uint16_t CST9217_REG_INFO_CHECK = 0xD1FC;
constexpr uint16_t CST9217_REG_PROJECT_AND_CHIP = 0xD204;
constexpr uint16_t CST9217_REG_FIRMWARE = 0xD208;
constexpr uint8_t CST9217_ACK = 0xAB;
constexpr uint8_t CST9217_TOUCH_EVENT_PRESS = 0x06;
constexpr uint16_t CST9217_CHIP_ID = 0x9217;
constexpr uint16_t CST9220_CHIP_ID = 0x9220;

struct TouchRegisterMap {
    uint8_t gesture_reg;
    uint8_t finger_count_reg;
    uint8_t x_pos_reg;
    uint8_t chip_id_reg;
    uint8_t firmware_reg;
};

touch_controller_type_t getTargetTouchControllerType()
{
#if QNOB_TOUCH_CONTROLLER == QNOB_TOUCH_CONTROLLER_FT6X36
    return TOUCH_CONTROLLER_FT6X36;
#elif QNOB_TOUCH_CONTROLLER == QNOB_TOUCH_CONTROLLER_CST9217
    return TOUCH_CONTROLLER_CST9217;
#else
    return TOUCH_CONTROLLER_CST816S;
#endif
}

const TouchRegisterMap& getTouchRegisterMap(touch_controller_type_t controller_type)
{
    static const TouchRegisterMap cst816s_map = {
        .gesture_reg = CST816S_REG_GESTURE_ID,
        .finger_count_reg = CST816S_REG_FINGER_NUM,
        .x_pos_reg = CST816S_REG_XPOS_H,
        .chip_id_reg = CST816S_REG_CHIP_ID,
        .firmware_reg = CST816S_REG_FW_VERSION,
    };
    static const TouchRegisterMap ft6x36_map = {
        .gesture_reg = FT6X36_REG_GESTURE_ID,
        .finger_count_reg = FT6X36_REG_FINGER_NUM,
        .x_pos_reg = FT6X36_REG_XPOS_H,
        .chip_id_reg = FT6X36_REG_CHIP_ID,
        .firmware_reg = FT6X36_REG_FW_VERSION,
    };

    if (controller_type == TOUCH_CONTROLLER_FT6X36) {
        return ft6x36_map;
    }
    return cst816s_map;
}

uint8_t normalizeTouchPointCount(touch_controller_type_t controller_type, uint8_t raw_points)
{
    if (controller_type == TOUCH_CONTROLLER_FT6X36) {
        return raw_points & 0x0F;
    }
    return raw_points;
}

uint32_t decodeLittleEndianU32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

void transformTouchCoordinates(uint16_t* x, uint16_t* y)
{
    if (x == nullptr || y == nullptr) {
        return;
    }

    uint16_t tx = *x;
    uint16_t ty = *y;

#if TOUCH_SWAP_XY
    const uint16_t tmp = tx;
    tx = ty;
    ty = tmp;
#endif

#if TOUCH_MIRROR_X
    if (tx < DISPLAY_WIDTH) {
        tx = static_cast<uint16_t>((DISPLAY_WIDTH - 1) - tx);
    }
#endif

#if TOUCH_MIRROR_Y
    if (ty < DISPLAY_HEIGHT) {
        ty = static_cast<uint16_t>((DISPLAY_HEIGHT - 1) - ty);
    }
#endif

    *x = tx;
    *y = ty;
}

}  // namespace

TouchPanel::TouchPanel(int sda, int scl, int rst, int irq)
    : sda_pin(sda), scl_pin(scl), rst_pin(rst), irq_pin(irq),
      i2c_addr(TP_I2C_ADDR), controller_type(getTargetTouchControllerType()), initialized(false),
      first_press(true), first_release(true),
      pressed(false), released(true), hold(false), long_hold(false),
      has_new_press(false), has_new_release(false), has_new_hold_release(false),
      has_new_gesture(false), last_gesture(GESTURE_NONE),
      tap_confirmed(false), swipe_candidate(false), max_move_sq(0),
      press_time(0), release_time(0), last_press_duration_ms(0),
      hold_threshold_ms(1000), long_hold_threshold_ms(10000), release_threshold_ms(30),
      touch_x(0), touch_y(0), press_x(0), press_y(0), release_x(0), release_y(0),
      gesture(GESTURE_NONE), i2c_dev_handle(nullptr),
      controller_chip_id16(0), controller_fw_version32(0)
{
}

TouchPanel::~TouchPanel()
{
    if (i2c_dev_handle != nullptr) {
        i2c_master_bus_rm_device(i2c_dev_handle);
        i2c_dev_handle = nullptr;
    }
}

const char* TouchPanel::getControllerName() const
{
    switch (controller_type) {
        case TOUCH_CONTROLLER_CST816S:
            return "CST816S";
        case TOUCH_CONTROLLER_FT6X36:
            return "FT6x36-compatible";
        case TOUCH_CONTROLLER_CST9217:
            return "CST9217";
        default:
            return "unknown";
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

esp_err_t TouchPanel::i2c_read_reg16(uint16_t reg, uint8_t* data, size_t len)
{
    if (i2c_dev_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t tx[2] = {
        static_cast<uint8_t>((reg >> 8) & 0xFF),
        static_cast<uint8_t>(reg & 0xFF),
    };
    return i2c_master_transmit_receive(i2c_dev_handle, tx, sizeof(tx), data, len, I2C_MASTER_TIMEOUT_MS);
}

esp_err_t TouchPanel::i2c_write_cmd16(uint16_t reg)
{
    if (i2c_dev_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t tx[2] = {
        static_cast<uint8_t>((reg >> 8) & 0xFF),
        static_cast<uint8_t>(reg & 0xFF),
    };
    return i2c_master_transmit(i2c_dev_handle, tx, sizeof(tx), I2C_MASTER_TIMEOUT_MS);
}

esp_err_t TouchPanel::initializeCst9217()
{
    esp_err_t info_mode_err = i2c_write_cmd16(CST9217_REG_INFO_MODE);
    if (info_mode_err != ESP_OK) {
        ESP_LOGW(TAG, "CST9217 command-mode probe failed: %s",
                 esp_err_to_name(info_mode_err));
        return ESP_OK;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t buffer[8] = {0};

    esp_err_t err = i2c_read_reg16(CST9217_REG_INFO_CHECK, buffer, 4);
    if (err == ESP_OK) {
        const uint32_t check_code = decodeLittleEndianU32(buffer);
        if ((check_code & 0xFFFF0000U) != 0xCACA0000U) {
            ESP_LOGW(TAG, "CST9217 probe returned unexpected check code 0x%08lX",
                     static_cast<unsigned long>(check_code));
        }
    }

    err = i2c_read_reg16(CST9217_REG_RESOLUTION, buffer, 4);
    if (err == ESP_OK) {
        const uint16_t res_x = static_cast<uint16_t>((buffer[1] << 8) | buffer[0]);
        const uint16_t res_y = static_cast<uint16_t>((buffer[3] << 8) | buffer[2]);
        ESP_LOGI(TAG, "CST9217 reported resolution %ux%u", res_x, res_y);
    }

    err = i2c_read_reg16(CST9217_REG_PROJECT_AND_CHIP, buffer, 4);
    if (err == ESP_OK) {
        controller_chip_id16 = static_cast<uint16_t>((buffer[3] << 8) | buffer[2]);
        if (controller_chip_id16 != CST9217_CHIP_ID && controller_chip_id16 != CST9220_CHIP_ID) {
            ESP_LOGW(TAG, "Unexpected CST92xx chip id 0x%04X", controller_chip_id16);
        }
    }

    err = i2c_read_reg16(CST9217_REG_FIRMWARE, buffer, 8);
    if (err == ESP_OK) {
        controller_fw_version32 = decodeLittleEndianU32(buffer);
    }

    return ESP_OK;
}

esp_err_t TouchPanel::initialize()
{
    ESP_LOGI(TAG, "TouchPanel::initialize() called");
    ESP_LOGI(TAG, "Initializing %s touch controller (addr=0x%02X)...",
             getControllerName(), i2c_addr);

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
                .disable_ack_check = true,
            },
        };
        esp_err_t add_dev_err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &i2c_dev_handle);
        if (add_dev_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add touch I2C device: %s", esp_err_to_name(add_dev_err));
            return add_dev_err;
        }
    }

    reset();

    if (controller_type == TOUCH_CONTROLLER_CST9217) {
        vTaskDelay(pdMS_TO_TICKS(30));
        esp_err_t cst_err = initializeCst9217();
        if (cst_err != ESP_OK) {
            ESP_LOGW(TAG, "CST9217 metadata probe failed: %s", esp_err_to_name(cst_err));
        }
    } else if (controller_type == TOUCH_CONTROLLER_FT6X36) {
        esp_err_t mode_err = ESP_FAIL;
        for (int i = 0; i < 4; ++i) {
            mode_err = i2c_write_reg(FT6X36_REG_MONITOR_CTRL, 0x00);
            if (mode_err == ESP_OK) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (mode_err != ESP_OK) {
            ESP_LOGW(TAG, "%s normal-mode command failed: %s",
                     getControllerName(), esp_err_to_name(mode_err));
        }

        esp_err_t power_err = i2c_write_reg(FT6X36_REG_POWER_MODE, FT6X36_POWER_MODE_ACTIVE);
        if (power_err != ESP_OK) {
            ESP_LOGW(TAG, "%s active power-mode command failed: %s",
                     getControllerName(), esp_err_to_name(power_err));
        }
    }

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

    if (controller_type == TOUCH_CONTROLLER_CST9217 && controller_chip_id16 != 0) {
        ESP_LOGI(TAG, "Touch initialization complete (%s, chip=0x%04X, fw=0x%08lX)",
                 getControllerName(),
                 controller_chip_id16,
                 static_cast<unsigned long>(controller_fw_version32));
    } else {
        const uint8_t chip_id = getChipId();
        const uint8_t fw_version = getFirmwareVersion();
        ESP_LOGI(TAG, "Touch initialization complete (%s, chip=0x%02X, fw=0x%02X)",
                 getControllerName(), chip_id, fw_version);
    }
    return ESP_OK;
}

uint8_t TouchPanel::getChipId()
{
    if (controller_type == TOUCH_CONTROLLER_CST9217) {
        return static_cast<uint8_t>(controller_chip_id16 & 0xFF);
    }

    const TouchRegisterMap& reg_map = getTouchRegisterMap(controller_type);
    uint8_t chip_id = 0;
    if (i2c_read_reg(reg_map.chip_id_reg, &chip_id, 1) != ESP_OK) {
        return 0;
    }
    return chip_id;
}

uint8_t TouchPanel::getFirmwareVersion()
{
    if (controller_type == TOUCH_CONTROLLER_CST9217) {
        return static_cast<uint8_t>(controller_fw_version32 & 0xFF);
    }

    const TouchRegisterMap& reg_map = getTouchRegisterMap(controller_type);
    uint8_t version = 0;
    if (i2c_read_reg(reg_map.firmware_reg, &version, 1) != ESP_OK) {
        return 0;
    }
    return version;
}

bool TouchPanel::readCst9217TouchData(touch_data_t* data)
{
    if (data == nullptr) {
        return false;
    }

    data->available = false;
    data->fingers = 0;
    data->gesture = GESTURE_NONE;

    uint8_t read_buffer[15] = {0};
    if (i2c_read_reg16(CST9217_REG_TOUCH_DATA, read_buffer, sizeof(read_buffer)) != ESP_OK) {
        data->available = false;
        return false;
    }

    uint8_t ack_buffer[3] = {
        static_cast<uint8_t>((CST9217_REG_TOUCH_DATA >> 8) & 0xFF),
        static_cast<uint8_t>(CST9217_REG_TOUCH_DATA & 0xFF),
        CST9217_ACK,
    };
    esp_err_t ack_err = i2c_master_transmit(i2c_dev_handle, ack_buffer, sizeof(ack_buffer), I2C_MASTER_TIMEOUT_MS);
    if (ack_err != ESP_OK) {
        ESP_LOGD(TAG, "CST9217 touch ACK write failed: %s", esp_err_to_name(ack_err));
    }

    if (read_buffer[6] != CST9217_ACK) {
        data->available = false;
        return false;
    }

    const uint8_t point_count = read_buffer[5] & 0x7F;
    if (point_count == 0) {
        data->available = false;
        return false;
    }

    const uint8_t* point = read_buffer;
    const uint8_t point_event = point[0] & 0x0F;
    if (point_event != CST9217_TOUCH_EVENT_PRESS) {
        data->available = false;
        return false;
    }

    data->fingers = point_count;
    data->x = static_cast<uint16_t>((point[1] << 4) | (point[3] >> 4));
    data->y = static_cast<uint16_t>((point[2] << 4) | (point[3] & 0x0F));
    data->gesture = GESTURE_NONE;

    transformTouchCoordinates(&data->x, &data->y);

    if (data->x >= DISPLAY_WIDTH) {
        data->x = DISPLAY_WIDTH - 1;
    }
    if (data->y >= DISPLAY_HEIGHT) {
        data->y = DISPLAY_HEIGHT - 1;
    }

    data->available = true;
    return true;
}

bool TouchPanel::readTouchData(touch_data_t* data)
{
    if (controller_type == TOUCH_CONTROLLER_CST9217) {
        return readCst9217TouchData(data);
    }

    const TouchRegisterMap& reg_map = getTouchRegisterMap(controller_type);

    uint8_t gesture_id = 0;
    if (i2c_read_reg(reg_map.gesture_reg, &gesture_id, 1) == ESP_OK) {
        data->gesture = static_cast<touch_gesture_t>(gesture_id);
    } else {
        data->gesture = GESTURE_NONE;
    }

    uint8_t points = 0;
    if (i2c_read_reg(reg_map.finger_count_reg, &points, 1) != ESP_OK) {
        data->available = false;
        return false;
    }
    points = normalizeTouchPointCount(controller_type, points);

    if (points == 0) {
        data->available = false;
        return false;
    }

    uint8_t xy[4] = {0};
    if (i2c_read_reg(reg_map.x_pos_reg, xy, sizeof(xy)) != ESP_OK) {
        data->available = false;
        return false;
    }

    data->fingers = points;
    data->x = ((xy[0] & 0x0F) << 8) | xy[1];
    data->y = ((xy[2] & 0x0F) << 8) | xy[3];

    transformTouchCoordinates(&data->x, &data->y);

    if (data->x >= DISPLAY_WIDTH) {
        data->x = DISPLAY_WIDTH - 1;
    }
    if (data->y >= DISPLAY_HEIGHT) {
        data->y = DISPLAY_HEIGHT - 1;
    }
    data->available = true;

    return data->available;
}

void TouchPanel::prepareCst9217ForSleep()
{
    // On the S3 AMOLED 1.75 board the CST9217 INT pin is the wake source.
    // Sending the controller into its deep sleep mode suppresses the wake IRQ,
    // so leave it awake and let the panel assert INT on the next tap.
    ESP_LOGI(TAG, "CST9217 left in wake-capable mode for deep sleep resume");
}

void TouchPanel::prepareForSleep()
{
    if (!initialized || i2c_dev_handle == nullptr) {
        return;
    }

    if (controller_type == TOUCH_CONTROLLER_CST9217) {
        prepareCst9217ForSleep();
        return;
    }

    if (controller_type == TOUCH_CONTROLLER_FT6X36) {
        esp_err_t err = i2c_write_reg(FT6X36_REG_POWER_MODE, FT6X36_POWER_MODE_MONITOR);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s monitor-mode command failed: %s",
                     getControllerName(), esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "%s configured for wake-on-touch monitor mode", getControllerName());
        return;
    }

    ESP_LOGI(TAG, "%s uses its default low-power touch behavior before sleep", getControllerName());
}

void TouchPanel::handleTouchPanel()
{
    if (!initialized) {
        return;
    }

    int64_t current_time = esp_timer_get_time() / 1000;

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
