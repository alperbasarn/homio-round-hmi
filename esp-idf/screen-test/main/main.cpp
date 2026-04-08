#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "hal_config.h"
#include "hal_init.h"

static const char *TAG = "SCREEN_TEST";
static constexpr int LCD_X_OFFSET = 6;
static constexpr int LCD_DRAWABLE_WIDTH = DISPLAY_WIDTH;

static const sh8601_lcd_init_cmd_t k_lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 80},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 1},
    {0x63, (uint8_t[]){0xFF}, 1, 1},
    {0x51, (uint8_t[]){0x00}, 1, 1},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static esp_lcd_panel_handle_t s_panel = nullptr;
static SemaphoreHandle_t s_flush_done = nullptr;

static bool on_lcd_flush_done(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata,
    void *user_ctx)
{
    BaseType_t high_task_wakeup = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static void draw_bitmap(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x >= LCD_DRAWABLE_WIDTH || y >= DISPLAY_HEIGHT) {
        return;
    }
    if (x + w > LCD_DRAWABLE_WIDTH || y + h > DISPLAY_HEIGHT) {
        return;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
        s_panel, x + LCD_X_OFFSET, y, x + LCD_X_OFFSET + w, y + h, pixels));
    // Wait transfer complete before reusing/freeing pixel buffers.
    xSemaphoreTake(s_flush_done, portMAX_DELAY);
}

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    // SH8601 path is most stable with even x/y and even width/height.
    x &= ~1;
    y &= ~1;
    w &= ~1;
    h &= ~1;
    if (w <= 0 || h <= 0) {
        return;
    }

    uint16_t *block = (uint16_t *)heap_caps_malloc((size_t)w * 2 * sizeof(uint16_t), MALLOC_CAP_DMA);
    assert(block);
    for (int i = 0; i < w * 2; ++i) {
        block[i] = color;
    }
    for (int yy = 0; yy < h; yy += 2) {
        draw_bitmap(x, y + yy, w, 2, block);
    }
    free(block);
}

static const uint8_t *glyph_5x7(char c)
{
    // 5-bit rows, top-to-bottom, only chars needed by this test screen.
    static const uint8_t g_space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t g_colon[7] = {0, 4, 0, 0, 4, 0, 0};
    static const uint8_t g_comma[7] = {0, 0, 0, 0, 0, 4, 8};
    static const uint8_t g_0[7] = {14, 17, 19, 21, 25, 17, 14};
    static const uint8_t g_1[7] = {4, 12, 4, 4, 4, 4, 14};
    static const uint8_t g_2[7] = {14, 17, 1, 2, 4, 8, 31};
    static const uint8_t g_3[7] = {30, 1, 1, 14, 1, 1, 30};
    static const uint8_t g_4[7] = {2, 6, 10, 18, 31, 2, 2};
    static const uint8_t g_5[7] = {31, 16, 16, 30, 1, 1, 30};
    static const uint8_t g_6[7] = {14, 16, 16, 30, 17, 17, 14};
    static const uint8_t g_7[7] = {31, 1, 2, 4, 8, 8, 8};
    static const uint8_t g_8[7] = {14, 17, 17, 14, 17, 17, 14};
    static const uint8_t g_9[7] = {14, 17, 17, 15, 1, 1, 14};
    static const uint8_t g_c[7] = {14, 17, 16, 16, 16, 17, 14};
    static const uint8_t g_d[7] = {30, 17, 17, 17, 17, 17, 30};
    static const uint8_t g_e[7] = {31, 16, 16, 30, 16, 16, 31};
    static const uint8_t g_h[7] = {17, 17, 17, 31, 17, 17, 17};
    static const uint8_t g_l[7] = {16, 16, 16, 16, 16, 16, 31};
    static const uint8_t g_o[7] = {14, 17, 17, 17, 17, 17, 14};
    static const uint8_t g_r[7] = {30, 17, 17, 30, 20, 18, 17};
    static const uint8_t g_t[7] = {31, 4, 4, 4, 4, 4, 4};
    static const uint8_t g_u[7] = {17, 17, 17, 17, 17, 17, 14};
    static const uint8_t g_w[7] = {17, 17, 17, 21, 21, 21, 10};
    static const uint8_t g_x[7] = {17, 17, 10, 4, 10, 17, 17};
    static const uint8_t g_y[7] = {17, 17, 10, 4, 4, 4, 4};

    switch (c) {
    case ' ': return g_space;
    case ':': return g_colon;
    case ',': return g_comma;
    case '0': return g_0;
    case '1': return g_1;
    case '2': return g_2;
    case '3': return g_3;
    case '4': return g_4;
    case '5': return g_5;
    case '6': return g_6;
    case '7': return g_7;
    case '8': return g_8;
    case '9': return g_9;
    case 'C': return g_c;
    case 'D': return g_d;
    case 'E': return g_e;
    case 'H': return g_h;
    case 'L': return g_l;
    case 'O': return g_o;
    case 'R': return g_r;
    case 'T': return g_t;
    case 'U': return g_u;
    case 'W': return g_w;
    case 'X': return g_x;
    case 'Y': return g_y;
    default: return g_space;
    }
}

static void draw_char(char c, int x, int y, int scale, uint16_t fg, uint16_t bg)
{
    int cw = 5 * scale;
    int ch = 7 * scale;
    cw &= ~1;
    ch &= ~1;
    x &= ~1;
    y &= ~1;
    const uint8_t *rows = glyph_5x7(c);

    uint16_t *buf = (uint16_t *)heap_caps_malloc((size_t)cw * (size_t)ch * sizeof(uint16_t), MALLOC_CAP_DMA);
    assert(buf);

    for (int py = 0; py < ch; ++py) {
        const int glyph_row = py / scale;
        for (int px = 0; px < cw; ++px) {
            const int glyph_col = px / scale;
            const bool on = (rows[glyph_row] >> (4 - glyph_col)) & 0x1;
            buf[(size_t)py * (size_t)cw + (size_t)px] = on ? fg : bg;
        }
    }

    draw_bitmap(x, y, cw, ch, buf);
    free(buf);
}

static void draw_text(const char *text, int x, int y, int scale, uint16_t fg, uint16_t bg)
{
    for (int i = 0; text[i] != '\0'; ++i) {
        draw_char(text[i], x + i * (6 * scale), y, scale, fg, bg);
    }
}

static bool read_touch_xy(i2c_master_dev_handle_t dev, uint16_t *x, uint16_t *y)
{
    uint8_t points = 0;
    uint8_t reg = 0x02;
    if (i2c_master_transmit_receive(dev, &reg, 1, &points, 1, I2C_MASTER_TIMEOUT_MS) != ESP_OK) {
        return false;
    }
    if (points != 1) {
        return false;
    }

    uint8_t buf[4] = {};
    reg = 0x03;
    if (i2c_master_transmit_receive(dev, &reg, 1, buf, sizeof(buf), I2C_MASTER_TIMEOUT_MS) != ESP_OK) {
        return false;
    }

    *x = (uint16_t)(((buf[0] & 0x0F) << 8) | buf[1]);
    *y = (uint16_t)(((buf[2] & 0x0F) << 8) | buf[3]);
    if (*x >= DISPLAY_WIDTH) {
        *x = DISPLAY_WIDTH - 1;
    }
    if (*y >= DISPLAY_HEIGHT) {
        *y = DISPLAY_HEIGHT - 1;
    }
    return true;
}

static void init_touch_controller(i2c_master_dev_handle_t dev)
{
    // Match official example: switch touch IC to normal mode.
    uint8_t mode = 0x00;
    uint8_t packet[2] = {0x86, mode};
    for (int i = 0; i < 4; ++i) {
        if (i2c_master_transmit(dev, packet, sizeof(packet), I2C_MASTER_TIMEOUT_MS) == ESP_OK) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGW(TAG, "Touch normal mode write failed");
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting SH8601 test (official driver path)");
    ESP_ERROR_CHECK(board_hal_init());
    s_flush_done = xSemaphoreCreateBinary();
    assert(s_flush_done);

    spi_bus_config_t bus_cfg = {};
    bus_cfg.sclk_io_num = LCD_CLK_PIN;
    bus_cfg.data0_io_num = LCD_MOSI_PIN;
    bus_cfg.data1_io_num = LCD_MISO_PIN;
    bus_cfg.data2_io_num = LCD_D2_PIN;
    bus_cfg.data3_io_num = LCD_D3_PIN;
    bus_cfg.max_transfer_sz = DISPLAY_WIDTH * 60 * 2;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(LCD_CS_PIN, on_lcd_flush_done, nullptr);
    io_cfg.pclk_hz = 40 * 1000 * 1000;
    io_cfg.trans_queue_depth = 1;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io));

    sh8601_vendor_config_t vendor_cfg = {};
    vendor_cfg.flags.use_qspi_interface = 1;
    vendor_cfg.init_cmds = k_lcd_init_cmds;
    vendor_cfg.init_cmds_size = sizeof(k_lcd_init_cmds) / sizeof(k_lcd_init_cmds[0]);

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = LCD_RST_PIN;
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    panel_cfg.vendor_config = &vendor_cfg;

    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0x0000);
    draw_text("HELLO WORLD", 100, 200, 4, 0xFFFF, 0x0000);
    draw_text("TOUCH: X:0, Y:0", 120, 260, 2, 0xFFFF, 0x0000);
    ESP_LOGI(TAG, "Display test UI drawn");

    i2c_master_dev_handle_t touch_dev = nullptr;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = TP_I2C_ADDR;
    dev_cfg.scl_speed_hz = 300000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(hal_i2c_get_bus_handle(), &dev_cfg, &touch_dev));
    init_touch_controller(touch_dev);

    char touch_line[32];
    uint16_t x = 0;
    uint16_t y = 0;
    while (true) {
        if (read_touch_xy(touch_dev, &x, &y)) {
            snprintf(touch_line, sizeof(touch_line), "TOUCH: X:%u, Y:%u", (unsigned)x, (unsigned)y);
            fill_rect(0, 258, DISPLAY_WIDTH, 32, 0x0000);
            draw_text(touch_line, 80, 260, 2, 0xFFFF, 0x0000);
            ESP_LOGI(TAG, "Touch x=%u y=%u", (unsigned)x, (unsigned)y);
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
