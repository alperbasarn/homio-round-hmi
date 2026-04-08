#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Hardware Abstraction Layer
 *
 * This function initializes all hardware-related configurations including:
 * - GPIO pins for display, touch, and other peripherals
 * - I2C bus for touch panel and IMU
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t board_hal_init(void);

/**
 * @brief Initialize I2C master bus
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t hal_i2c_init(void);

/**
 * @brief Initialize GPIO pins
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t hal_gpio_init(void);

/**
 * @brief Get initialized I2C master bus handle
 *
 * @return i2c bus handle or NULL if I2C is not initialized
 */
i2c_master_bus_handle_t hal_i2c_get_bus_handle(void);

/**
 * @brief Configure and drive a TCA9554 EXIO pin (ESP32-C6 board only)
 *
 * @param pin_bit EXIO bit number (0-7)
 * @param level true=HIGH, false=LOW
 * @return ESP_OK on success
 */
esp_err_t hal_exio_set_pin_output_level(uint8_t pin_bit, bool level);

/**
 * @brief Clear TCA9554 INT# by reading the input register (ESP32-C6 only)
 *
 * The TCA9554 INT# output stays LOW until the input port register is read.
 * Call this before entering sleep so that a new touch event will assert INT#.
 *
 * @return ESP_OK on success
 */
esp_err_t hal_exio_clear_interrupt(void);

#ifdef __cplusplus
}
#endif
