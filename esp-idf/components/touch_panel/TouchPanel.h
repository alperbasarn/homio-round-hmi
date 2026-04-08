#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// CST816S Touch Controller Registers
#define CST816S_REG_GESTURE_ID      0x01
#define CST816S_REG_FINGER_NUM      0x02
#define CST816S_REG_XPOS_H          0x03
#define CST816S_REG_XPOS_L          0x04
#define CST816S_REG_YPOS_H          0x05
#define CST816S_REG_YPOS_L          0x06
#define CST816S_REG_CHIP_ID         0xA7
#define CST816S_REG_PROJ_ID         0xA8
#define CST816S_REG_FW_VERSION      0xA9
#define CST816S_REG_MOTION_MASK     0xEC
#define CST816S_REG_IRQ_PULSE_WIDTH 0xED
#define CST816S_REG_NOR_SCAN_PER    0xEE
#define CST816S_REG_MOTION_S1_ANGLE 0xEF
#define CST816S_REG_LP_SCAN_RAW1H   0xF0
#define CST816S_REG_LP_SCAN_RAW1L   0xF1
#define CST816S_REG_LP_SCAN_RAW2H   0xF2
#define CST816S_REG_LP_SCAN_RAW2L   0xF3
#define CST816S_REG_LP_AUTO_WAKEUP  0xF4
#define CST816S_REG_LP_SCAN_TH      0xF5
#define CST816S_REG_LP_SCAN_WIN     0xF6
#define CST816S_REG_LP_SCAN_FREQ    0xF7
#define CST816S_REG_LP_SCAN_IDAC    0xF8
#define CST816S_REG_AUTO_SLEEP_TIME 0xF9
#define CST816S_REG_IRQ_CTL         0xFA
#define CST816S_REG_AUTO_RESET      0xFB
#define CST816S_REG_LONG_PRESS_TIME 0xFC
#define CST816S_REG_IO_CTL          0xFD
#define CST816S_REG_DIS_AUTO_SLEEP  0xFE

// Gesture IDs
typedef enum {
    GESTURE_NONE = 0x00,
    GESTURE_SWIPE_UP = 0x01,
    GESTURE_SWIPE_DOWN = 0x02,
    GESTURE_SWIPE_LEFT = 0x03,
    GESTURE_SWIPE_RIGHT = 0x04,
    GESTURE_SINGLE_CLICK = 0x05,
    GESTURE_DOUBLE_CLICK = 0x0B,
    GESTURE_LONG_PRESS = 0x0C,
    GESTURE_TAP_RELEASE = 0x20,
    GESTURE_HOLD_RELEASE = 0x21
} touch_gesture_t;

// Touch data structure
typedef struct {
    uint16_t x;
    uint16_t y;
    touch_gesture_t gesture;
    uint8_t fingers;
    bool available;
} touch_data_t;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class TouchPanel {
private:
    int sda_pin;
    int scl_pin;
    int rst_pin;
    int irq_pin;
    uint8_t i2c_addr;

    bool initialized;
    bool first_press;
    bool first_release;

    bool pressed;
    bool released;
    bool hold;
    bool long_hold;
    bool has_new_press;
    bool has_new_release;
    bool has_new_hold_release;
    bool has_new_gesture;      // True when a gesture event is detected
    touch_gesture_t last_gesture;  // Stores the latest gesture event
    bool tap_confirmed;
    bool swipe_candidate;
    int max_move_sq;

    int64_t press_time;
    int64_t release_time;
    int64_t last_press_duration_ms;
    int hold_threshold_ms;
    int long_hold_threshold_ms;
    int release_threshold_ms;

    int touch_x;
    int touch_y;
    int press_x;
    int press_y;
    int release_x;
    int release_y;
    touch_gesture_t gesture;
    i2c_master_dev_handle_t i2c_dev_handle;

    static constexpr int TAP_RELEASE_RADIUS_PX = 25;
    static constexpr int TAP_CONFIRM_MS = 100;
    static constexpr int SWIPE_MIN_PX = 40;
    static constexpr int REPRESS_MIN_MS = 8;

    // I2C communication
    esp_err_t i2c_read_reg(uint8_t reg, uint8_t* data, size_t len);
    esp_err_t i2c_write_reg(uint8_t reg, uint8_t data);

    // Reset the touch controller
    void reset();

public:
    TouchPanel(int sda, int scl, int rst, int irq);
    ~TouchPanel();

    esp_err_t initialize();
    void handleTouchPanel();

    bool isPressed() const { return pressed; }
    bool isHold() const { return hold; }
    bool isLongHold() const { return long_hold; }
    bool isReleased() const { return released; }
    bool getHasNewPress() const { return has_new_press; }
    bool getHasNewRelease() const { return has_new_release; }
    bool getHasNewHoldRelease() const { return has_new_hold_release; }
    bool getHasNewGesture() const { return has_new_gesture; }
    int getTouchX() const { return touch_x; }
    int getTouchY() const { return touch_y; }
    touch_gesture_t getGesture() const { return gesture; }
    touch_gesture_t getLastGesture() const { return last_gesture; }
    int64_t getLastPressDurationMs() const { return last_press_duration_ms; }

    // Read raw touch data
    bool readTouchData(touch_data_t* data);

    // Prepare touch IC for deep sleep (monitor mode for wake-on-touch)
    void prepareForSleep();

    // Get chip information
    uint8_t getChipId();
    uint8_t getFirmwareVersion();
};

#endif
