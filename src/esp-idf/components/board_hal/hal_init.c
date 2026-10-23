#include "hal_config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "HAL";
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
#if CONFIG_IDF_TARGET_ESP32C6
static i2c_master_dev_handle_t s_exio_dev_handle = NULL;
#endif

#if CONFIG_IDF_TARGET_ESP32C6
static esp_err_t exio_read_reg(uint8_t reg, uint8_t* value)
{
    if (s_exio_dev_handle == NULL || value == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_exio_dev_handle, &reg, 1, value, 1, I2C_MASTER_TIMEOUT_MS);
}

static esp_err_t exio_write_reg(uint8_t reg, uint8_t value)
{
    if (s_exio_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t tx[2] = {reg, value};
    return i2c_master_transmit(s_exio_dev_handle, tx, sizeof(tx), I2C_MASTER_TIMEOUT_MS);
}

static esp_err_t hal_power_latch_init(void)
{
    if (s_i2c_bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_exio_dev_handle == NULL) {
        i2c_device_config_t exio_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = EXIO_I2C_ADDR,
            .scl_speed_hz = 100000,
            .scl_wait_us = 0,
            .flags = {
                .disable_ack_check = false,
            },
        };
        esp_err_t add_err = i2c_master_bus_add_device(s_i2c_bus_handle, &exio_cfg, &s_exio_dev_handle);
        if (add_err != ESP_OK) {
            ESP_LOGW(TAG, "EXIO device add failed at 0x%02X: %s", EXIO_I2C_ADDR, esp_err_to_name(add_err));
            return add_err;
        }
    }

    // TCA9554 register map: 0x03=config, 0x01=output port.
    uint8_t cfg = 0xFF;
    esp_err_t err = exio_read_reg(0x03, &cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "EXIO config read failed at 0x%02X: %s", EXIO_I2C_ADDR, esp_err_to_name(err));
        return err;
    }

    cfg &= (uint8_t)~(1U << EXIO_BAT_EN_BIT);  // Configure BAT_EN pin as output.
    err = exio_write_reg(0x03, cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "EXIO config write failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t out = 0;
    err = exio_read_reg(0x01, &out);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "EXIO output read failed: %s", esp_err_to_name(err));
        return err;
    }

    out |= (uint8_t)(1U << EXIO_BAT_EN_BIT);   // Assert BAT_EN latch high.
    err = exio_write_reg(0x01, out);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "EXIO output write failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Power latch enabled via EXIO pin %d", EXIO_BAT_EN_BIT);
    return ESP_OK;
}
#endif

esp_err_t hal_i2c_init(void)
{
    if (s_i2c_bus_handle != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = TP_SDA_PIN,
        .scl_io_num = TP_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus_handle);
    if (err == ESP_ERR_INVALID_STATE) {
        err = i2c_master_get_bus_handle(I2C_MASTER_NUM, &s_i2c_bus_handle);
        if (err == ESP_OK && s_i2c_bus_handle != NULL) {
            ESP_LOGW(TAG, "I2C bus already initialized, reusing existing handle");
            return ESP_OK;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C initialized on SDA=%d, SCL=%d", TP_SDA_PIN, TP_SCL_PIN);
    return ESP_OK;
}

i2c_master_bus_handle_t hal_i2c_get_bus_handle(void)
{
    return s_i2c_bus_handle;
}

esp_err_t hal_exio_set_pin_output_level(uint8_t pin_bit, bool level)
{
#if CONFIG_IDF_TARGET_ESP32C6
    if (pin_bit > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_i2c_bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_exio_dev_handle == NULL) {
        i2c_device_config_t exio_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = EXIO_I2C_ADDR,
            .scl_speed_hz = 100000,
            .scl_wait_us = 0,
            .flags = {
                .disable_ack_check = false,
            },
        };
        esp_err_t add_err = i2c_master_bus_add_device(s_i2c_bus_handle, &exio_cfg, &s_exio_dev_handle);
        if (add_err != ESP_OK) {
            return add_err;
        }
    }

    uint8_t cfg = 0xFF;
    esp_err_t err = exio_read_reg(0x03, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    cfg &= (uint8_t)~(1U << pin_bit);  // Configure as output.
    err = exio_write_reg(0x03, cfg);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t out = 0;
    err = exio_read_reg(0x01, &out);
    if (err != ESP_OK) {
        return err;
    }
    if (level) {
        out |= (uint8_t)(1U << pin_bit);
    } else {
        out &= (uint8_t)~(1U << pin_bit);
    }
    return exio_write_reg(0x01, out);
#else
    (void)pin_bit;
    (void)level;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t hal_exio_clear_interrupt(void)
{
#if CONFIG_IDF_TARGET_ESP32C6
    if (s_exio_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    // Reading the TCA9554 input register (0x00) clears the INT# output.
    uint8_t input_val = 0;
    esp_err_t err = exio_read_reg(0x00, &input_val);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TCA9554 INT cleared (input reg=0x%02X)", input_val);
    }
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t hal_gpio_init(void)
{
    // Initialize touch panel interrupt pin
#if TP_INT_PIN >= 0
    gpio_config_t tp_int_conf = {
        .pin_bit_mask = (1ULL << TP_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&tp_int_conf);
    ESP_LOGI(TAG, "Touch INT pin %d configured", TP_INT_PIN);
#endif

    // Initialize touch panel reset pin
#if TP_RST_PIN >= 0
    gpio_config_t tp_rst_conf = {
        .pin_bit_mask = (1ULL << TP_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&tp_rst_conf);
    gpio_set_level(TP_RST_PIN, 1);
    ESP_LOGI(TAG, "Touch RST pin %d configured", TP_RST_PIN);
#endif

    // Initialize LCD backlight pin
#if LCD_BL_PIN >= 0
    gpio_config_t bl_conf = {
        .pin_bit_mask = (1ULL << LCD_BL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_conf);
    gpio_set_level(LCD_BL_PIN, 1);  // Turn on backlight
    ESP_LOGI(TAG, "LCD backlight pin %d configured", LCD_BL_PIN);
#endif

    // Initialize LCD reset pin
#if LCD_RST_PIN >= 0
    gpio_config_t lcd_rst_conf = {
        .pin_bit_mask = (1ULL << LCD_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&lcd_rst_conf);
    gpio_set_level(LCD_RST_PIN, 1);
    ESP_LOGI(TAG, "LCD RST pin %d configured", LCD_RST_PIN);
#endif

    return ESP_OK;
}

esp_err_t board_hal_init(void)
{
    ESP_LOGI(TAG, "Initializing HAL...");

    ESP_LOGI(TAG, "Target board: %s (%dx%d)", QNOB_BOARD_NAME, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    esp_err_t err = hal_gpio_init();
    if (err != ESP_OK) {
        return err;
    }

    err = hal_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

#if CONFIG_IDF_TARGET_ESP32C6
    err = hal_power_latch_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Power latch setup failed (board may require holding PWR): %s", esp_err_to_name(err));
    }
#endif

    ESP_LOGI(TAG, "HAL initialization complete");
    return ESP_OK;
}
