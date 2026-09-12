#include "EspLcdSh8601DisplayFlushBackend.h"

#include "freertos/FreeRTOS.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_vendor.h"
#if CONFIG_QNOB_HW_ESP32S3_AMOLED_175
#include "esp_lcd_co5300.h"
#else
#include "esp_lcd_sh8601.h"
#endif
#include "esp_log.h"
#include "hal_config.h"

namespace {

constexpr const char* TAG = "EspLcdAmoledFlush";
constexpr uint32_t LCD_OPCODE_WRITE_CMD = 0x02;
constexpr TickType_t kFlushTimeoutTicks = pdMS_TO_TICKS(1000);
constexpr size_t kMaxTransferBytes =
    static_cast<size_t>(DISPLAY_WIDTH) * static_cast<size_t>(DISPLAY_HEIGHT) * sizeof(uint16_t);

#if CONFIG_QNOB_HW_ESP32S3_AMOLED_175
using AmoledInitCmd = co5300_lcd_init_cmd_t;
constexpr const char* kPanelDriverName = "CO5300";
#else
using AmoledInitCmd = sh8601_lcd_init_cmd_t;
constexpr const char* kPanelDriverName = "SH8601";
#endif

#if CONFIG_QNOB_HW_ESP32S3_AMOLED_175
static const AmoledInitCmd kPanelInitCmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, nullptr, 0, 600},
    {0x29, nullptr, 0, 0},
};
#else
static const AmoledInitCmd kPanelInitCmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 80},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 1},
    {0x63, (uint8_t[]){0xFF}, 1, 1},
    {0x51, (uint8_t[]){0x00}, 1, 1},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};
#endif

uint32_t makeQspiWriteCommand(uint32_t command) {
    command &= 0xff;
    command <<= 8;
    command |= LCD_OPCODE_WRITE_CMD << 24;
    return command;
}

}  // namespace

EspLcdSh8601DisplayFlushBackend::EspLcdSh8601DisplayFlushBackend()
    : ioHandle(nullptr),
      panelHandle(nullptr),
      flushDoneSemaphore(nullptr),
      initialized(false) {
}

EspLcdSh8601DisplayFlushBackend::~EspLcdSh8601DisplayFlushBackend() {
    if (panelHandle != nullptr) {
        esp_lcd_panel_del(panelHandle);
    }
    if (ioHandle != nullptr) {
        esp_lcd_panel_io_del(ioHandle);
    }
    if (flushDoneSemaphore != nullptr) {
        vSemaphoreDelete(flushDoneSemaphore);
    }
}

bool EspLcdSh8601DisplayFlushBackend::initialize() {
    return ensureInitialized();
}

void EspLcdSh8601DisplayFlushBackend::setBrightness(uint8_t brightness) {
    if (!ensureInitialized() || ioHandle == nullptr) {
        return;
    }

    const uint32_t command = makeQspiWriteCommand(0x51);
    esp_err_t err = esp_lcd_panel_io_tx_param(ioHandle, command, &brightness, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set panel brightness: %s", esp_err_to_name(err));
    }
}

void EspLcdSh8601DisplayFlushBackend::setDisplayOn(bool enabled) {
    if (!ensureInitialized() || panelHandle == nullptr) {
        return;
    }

    esp_err_t err = esp_lcd_panel_disp_on_off(panelHandle, enabled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set panel display state: %s", esp_err_to_name(err));
    }
}

int EspLcdSh8601DisplayFlushBackend::width() const {
    return DISPLAY_WIDTH;
}

int EspLcdSh8601DisplayFlushBackend::height() const {
    return DISPLAY_HEIGHT;
}

uint32_t EspLcdSh8601DisplayFlushBackend::preferredDrawBufferLines() const {
    // Keep AMOLED rendering chunked enough to preserve internal DMA memory
    // for Wi-Fi and other subsystems on the 8 MB PSRAM S3 board.
    return 30;
}

uint32_t EspLcdSh8601DisplayFlushBackend::minimumDrawBufferLines() const {
    return 10;
}

bool EspLcdSh8601DisplayFlushBackend::requiresEvenAreaAlignment() const {
    return true;
}

void EspLcdSh8601DisplayFlushBackend::beginWrite() {
    ensureInitialized();
}

void EspLcdSh8601DisplayFlushBackend::writeImage(int32_t x,
                                                 int32_t y,
                                                 int32_t widthPixels,
                                                 int32_t heightPixels,
                                                 const void* pixelData,
                                                 DisplayFlushFormat format) {
    if (!ensureInitialized()) {
        return;
    }

    const size_t pixelCount = static_cast<size_t>(widthPixels) * static_cast<size_t>(heightPixels);
    const void* preparedPixelData = preparePixelData(pixelData, pixelCount, format);
    if (preparedPixelData == nullptr) {
        ESP_LOGE(TAG, "Failed to prepare pixel data for %ldx%ld flush", widthPixels, heightPixels);
        return;
    }

    if (xSemaphoreTake(flushDoneSemaphore, 0) == pdTRUE) {
        // Drain any stale completion token before submitting a new transfer.
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(panelHandle,
                                              x,
                                              y,
                                              x + widthPixels,
                                              y + heightPixels,
                                              preparedPixelData);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
        return;
    }

    if (xSemaphoreTake(flushDoneSemaphore, kFlushTimeoutTicks) != pdTRUE) {
        ESP_LOGE(TAG, "Timed out waiting for %s flush completion", kPanelDriverName);
    }
}

void EspLcdSh8601DisplayFlushBackend::endWrite() {
}

bool EspLcdSh8601DisplayFlushBackend::ensureInitialized() {
    if (initialized) {
        return true;
    }

    if (flushDoneSemaphore == nullptr) {
        flushDoneSemaphore = xSemaphoreCreateBinary();
        if (flushDoneSemaphore == nullptr) {
            ESP_LOGE(TAG, "Failed to create flush completion semaphore");
            return false;
        }
    }

    spi_bus_config_t busConfig = {};
    busConfig.sclk_io_num = LCD_CLK_PIN;
    busConfig.data0_io_num = LCD_MOSI_PIN;
    busConfig.data1_io_num = LCD_MISO_PIN;
    busConfig.data2_io_num = LCD_D2_PIN;
    busConfig.data3_io_num = LCD_D3_PIN;
    busConfig.max_transfer_sz = kMaxTransferBytes;
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus for esp_lcd flush backend: %s", esp_err_to_name(err));
        return false;
    }

    if (ioHandle == nullptr) {
        esp_lcd_panel_io_spi_config_t ioConfig = {};
        ioConfig.cs_gpio_num = LCD_CS_PIN;
        ioConfig.dc_gpio_num = -1;
        ioConfig.spi_mode = 0;
        ioConfig.pclk_hz = 40 * 1000 * 1000;
        ioConfig.trans_queue_depth = 10;
        ioConfig.on_color_trans_done = notifyFlushDone;
        ioConfig.user_ctx = this;
        ioConfig.lcd_cmd_bits = 32;
        ioConfig.lcd_param_bits = 8;
        ioConfig.flags.quad_mode = true;
        err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                       &ioConfig,
                                       &ioHandle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create %s panel IO: %s", kPanelDriverName, esp_err_to_name(err));
            return false;
        }
    }

    if (panelHandle == nullptr) {
#if CONFIG_QNOB_HW_ESP32S3_AMOLED_175
        co5300_vendor_config_t vendorConfig = {};
#else
        sh8601_vendor_config_t vendorConfig = {};
#endif
        vendorConfig.flags.use_qspi_interface = 1;
        vendorConfig.init_cmds = kPanelInitCmds;
        vendorConfig.init_cmds_size = sizeof(kPanelInitCmds) / sizeof(kPanelInitCmds[0]);

        esp_lcd_panel_dev_config_t panelConfig = {};
        panelConfig.reset_gpio_num = LCD_RST_PIN;
        panelConfig.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panelConfig.bits_per_pixel = 16;
        panelConfig.vendor_config = &vendorConfig;

#if CONFIG_QNOB_HW_ESP32S3_AMOLED_175
        err = esp_lcd_new_panel_co5300(ioHandle, &panelConfig, &panelHandle);
#else
        err = esp_lcd_new_panel_sh8601(ioHandle, &panelConfig, &panelHandle);
#endif
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create %s panel handle: %s", kPanelDriverName, esp_err_to_name(err));
            return false;
        }

        err = esp_lcd_panel_set_gap(panelHandle, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set %s panel gap: %s", kPanelDriverName, esp_err_to_name(err));
        }

        err = esp_lcd_panel_reset(panelHandle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset %s panel: %s", kPanelDriverName, esp_err_to_name(err));
            return false;
        }

        err = esp_lcd_panel_init(panelHandle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize %s panel: %s", kPanelDriverName, esp_err_to_name(err));
            return false;
        }

        err = esp_lcd_panel_disp_on_off(panelHandle, true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to enable %s panel output: %s", kPanelDriverName, esp_err_to_name(err));
        }
    }

    initialized = true;
    ESP_LOGI(TAG, "Initialized esp_lcd %s AMOLED backend for %s", kPanelDriverName, QNOB_BOARD_NAME);
    return true;
}

const void* EspLcdSh8601DisplayFlushBackend::preparePixelData(const void* pixelData,
                                                              size_t pixelCount,
                                                              DisplayFlushFormat format) {
    if (pixelData == nullptr) {
        return nullptr;
    }

    if (format == DisplayFlushFormat::RGB565_SWAPPED) {
        return pixelData;
    }

    // The LVGL draw buffers are already DMA-capable internal memory.
    // Swap bytes in place instead of allocating a third staging buffer.
    uint16_t* source = const_cast<uint16_t*>(static_cast<const uint16_t*>(pixelData));
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint16_t color = source[i];
        source[i] = static_cast<uint16_t>((color << 8) | (color >> 8));
    }

    return source;
}

bool EspLcdSh8601DisplayFlushBackend::notifyFlushDone(esp_lcd_panel_io_handle_t panelIo,
                                                      esp_lcd_panel_io_event_data_t* eventData,
                                                      void* userCtx) {
    (void)panelIo;
    (void)eventData;

    BaseType_t highTaskWoken = pdFALSE;
    auto* self = static_cast<EspLcdSh8601DisplayFlushBackend*>(userCtx);
    if (self != nullptr && self->flushDoneSemaphore != nullptr) {
        xSemaphoreGiveFromISR(self->flushDoneSemaphore, &highTaskWoken);
    }
    return highTaskWoken == pdTRUE;
}
