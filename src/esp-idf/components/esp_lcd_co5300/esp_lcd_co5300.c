/*
 * SPDX-FileCopyrightText: 2026 OpenAI
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <sys/cdefs.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#include "esp_lcd_co5300.h"

#define LCD_OPCODE_WRITE_CMD        (0x02ULL)
#define LCD_OPCODE_WRITE_COLOR      (0x32ULL)

static const char *TAG = "co5300";

static esp_err_t panel_co5300_del(esp_lcd_panel_t *panel);
static esp_err_t panel_co5300_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_co5300_init(esp_lcd_panel_t *panel);
static esp_err_t panel_co5300_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_co5300_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_co5300_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_co5300_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_co5300_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_co5300_disp_on_off(esp_lcd_panel_t *panel, bool on_off);

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val;
    uint8_t colmod_val;
    const co5300_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    struct {
        unsigned int use_qspi_interface: 1;
        unsigned int reset_level: 1;
    } flags;
} co5300_panel_t;

static esp_err_t tx_param(co5300_panel_t *co5300,
                          esp_lcd_panel_io_handle_t io,
                          int lcd_cmd,
                          const void *param,
                          size_t param_size)
{
    if (co5300->flags.use_qspi_interface) {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
    }
    return esp_lcd_panel_io_tx_param(io, lcd_cmd, param, param_size);
}

static esp_err_t tx_color(co5300_panel_t *co5300,
                          esp_lcd_panel_io_handle_t io,
                          int lcd_cmd,
                          const void *param,
                          size_t param_size)
{
    if (co5300->flags.use_qspi_interface) {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_COLOR << 24;
    }
    return esp_lcd_panel_io_tx_color(io, lcd_cmd, param, param_size);
}

esp_err_t esp_lcd_new_panel_co5300(const esp_lcd_panel_io_handle_t io,
                                   const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t ret = ESP_OK;
    co5300_panel_t *co5300 = calloc(1, sizeof(co5300_panel_t));
    ESP_GOTO_ON_FALSE(co5300, ESP_ERR_NO_MEM, err, TAG, "no mem for co5300 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        co5300->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        co5300->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color element order");
        break;
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 16:
        co5300->colmod_val = 0x55;
        co5300->fb_bits_per_pixel = 16;
        break;
    case 18:
        co5300->colmod_val = 0x66;
        co5300->fb_bits_per_pixel = 24;
        break;
    case 24:
        co5300->colmod_val = 0x77;
        co5300->fb_bits_per_pixel = 24;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    co5300_vendor_config_t *vendor_config = (co5300_vendor_config_t *)panel_dev_config->vendor_config;
    co5300->io = io;
    co5300->reset_gpio_num = panel_dev_config->reset_gpio_num;
    if (vendor_config) {
        co5300->init_cmds = vendor_config->init_cmds;
        co5300->init_cmds_size = vendor_config->init_cmds_size;
        co5300->flags.use_qspi_interface = vendor_config->flags.use_qspi_interface;
    }
    co5300->flags.reset_level = panel_dev_config->flags.reset_active_high;

    co5300->base.del = panel_co5300_del;
    co5300->base.reset = panel_co5300_reset;
    co5300->base.init = panel_co5300_init;
    co5300->base.draw_bitmap = panel_co5300_draw_bitmap;
    co5300->base.invert_color = panel_co5300_invert_color;
    co5300->base.mirror = panel_co5300_mirror;
    co5300->base.swap_xy = panel_co5300_swap_xy;
    co5300->base.set_gap = panel_co5300_set_gap;
    co5300->base.disp_on_off = panel_co5300_disp_on_off;

    *ret_panel = &(co5300->base);
    ESP_LOGI(TAG, "LCD panel create success");
    return ESP_OK;

err:
    if (co5300) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(co5300);
    }
    return ret;
}

static esp_err_t panel_co5300_del(esp_lcd_panel_t *panel)
{
    co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
    if (co5300->reset_gpio_num >= 0) {
        gpio_reset_pin(co5300->reset_gpio_num);
    }
    free(co5300);
    return ESP_OK;
}

static esp_err_t panel_co5300_reset(esp_lcd_panel_t *panel)
{
    co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
    esp_lcd_panel_io_handle_t io = co5300->io;

    if (co5300->reset_gpio_num >= 0) {
        gpio_set_level(co5300->reset_gpio_num, co5300->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(co5300->reset_gpio_num, !co5300->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(150));
    } else {
        ESP_RETURN_ON_ERROR(tx_param(co5300, io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    return ESP_OK;
}

static const co5300_lcd_init_cmd_t vendor_specific_init_default[] = {
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xC4, (uint8_t []){0x80}, 1, 0},
    {0x53, (uint8_t []){0x20}, 1, 0},
    {0x63, (uint8_t []){0xFF}, 1, 0},
    {0x51, (uint8_t []){0xD0}, 1, 0},
    {0x29, NULL, 0, 10},
};

static esp_err_t panel_co5300_init(esp_lcd_panel_t *panel)
{
    co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
    esp_lcd_panel_io_handle_t io = co5300->io;
    const co5300_lcd_init_cmd_t *init_cmds = co5300->init_cmds;
    uint16_t init_cmds_size = co5300->init_cmds_size;
    bool is_cmd_overwritten = false;

    ESP_RETURN_ON_ERROR(tx_param(co5300, io, LCD_CMD_MADCTL, (uint8_t[]) {
        co5300->madctl_val,
    }, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(tx_param(co5300, io, LCD_CMD_COLMOD, (uint8_t[]) {
        co5300->colmod_val,
    }, 1), TAG, "send command failed");

    if (init_cmds == NULL) {
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(vendor_specific_init_default[0]);
    }

    for (int i = 0; i < init_cmds_size; i++) {
        switch (init_cmds[i].cmd) {
        case LCD_CMD_MADCTL:
            is_cmd_overwritten = true;
            co5300->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        case LCD_CMD_COLMOD:
            is_cmd_overwritten = true;
            co5300->colmod_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        default:
            is_cmd_overwritten = false;
            break;
        }

        if (is_cmd_overwritten) {
            ESP_LOGW(TAG, "Command %02Xh overridden by vendor init sequence", init_cmds[i].cmd);
        }

        ESP_RETURN_ON_ERROR(tx_param(co5300, io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes),
                            TAG,
                            "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }

    return ESP_OK;
}

static esp_err_t panel_co5300_draw_bitmap(esp_lcd_panel_t *panel,
                                          int x_start,
                                          int y_start,
                                          int x_end,
                                          int y_end,
                                          const void *color_data)
{
    co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
    esp_lcd_panel_io_handle_t io = co5300->io;
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");

    x_start += co5300->x_gap;
    x_end += co5300->x_gap;
    y_start += co5300->y_gap;
    y_end += co5300->y_gap;

    ESP_RETURN_ON_ERROR(tx_param(co5300, io, LCD_CMD_CASET, (uint8_t[]) {
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF,
    }, 4), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(tx_param(co5300, io, LCD_CMD_RASET, (uint8_t[]) {
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF,
    }, 4), TAG, "send command failed");

    size_t len = (x_end - x_start) * (y_end - y_start) * co5300->fb_bits_per_pixel / 8;
    ESP_RETURN_ON_ERROR(tx_color(co5300, io, LCD_CMD_RAMWR, color_data, len), TAG, "send color data failed");

    return ESP_OK;
}

static esp_err_t panel_co5300_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
    return tx_param(co5300, co5300->io, invert_color_data ? LCD_CMD_INVON : LCD_CMD_INVOFF, NULL, 0);
}

static esp_err_t panel_co5300_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
    esp_err_t ret = ESP_OK;

    if (mirror_x) {
        co5300->madctl_val |= BIT(6);
    } else {
        co5300->madctl_val &= ~BIT(6);
    }
    if (mirror_y) {
        ESP_LOGE(TAG, "mirror_y is not supported by this panel");
        ret = ESP_ERR_NOT_SUPPORTED;
    }

    ESP_RETURN_ON_ERROR(tx_param(co5300, co5300->io, LCD_CMD_MADCTL, (uint8_t[]) {
        co5300->madctl_val,
    }, 1), TAG, "send command failed");
    return ret;
}

static esp_err_t panel_co5300_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    (void)panel;
    (void)swap_axes;
    ESP_LOGE(TAG, "swap_xy is not supported by this panel");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t panel_co5300_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
    co5300->x_gap = x_gap;
    co5300->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_co5300_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
    return tx_param(co5300, co5300->io, on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
}
