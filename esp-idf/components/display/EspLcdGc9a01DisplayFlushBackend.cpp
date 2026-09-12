#include "EspLcdGc9a01DisplayFlushBackend.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "hal_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr const char* TAG = "EspLcdGc9a01Flush";
constexpr TickType_t kFlushTimeoutTicks = pdMS_TO_TICKS(1000);

#if !QNOB_DISPLAY_IS_QSPI
constexpr size_t kMaxTransferBytes =
    static_cast<size_t>(DISPLAY_WIDTH) * static_cast<size_t>(DISPLAY_HEIGHT) * sizeof(uint16_t);

struct LcdInitCommand {
    uint8_t command;
    const void* data;
    size_t dataLength;
    uint16_t delayMs;
};

static const LcdInitCommand kPanelInitCmds[] = {
    {0xEF, nullptr, 0, 0},
    {0xEB, (uint8_t[]){0x14}, 1, 0},
    {0xFE, nullptr, 0, 0},
    {0xEF, nullptr, 0, 0},
    {0xEB, (uint8_t[]){0x14}, 1, 0},
    {0x84, (uint8_t[]){0x40}, 1, 0},
    {0x85, (uint8_t[]){0xFF}, 1, 0},
    {0x86, (uint8_t[]){0xFF}, 1, 0},
    {0x87, (uint8_t[]){0xFF}, 1, 0},
    {0x88, (uint8_t[]){0x0A}, 1, 0},
    {0x89, (uint8_t[]){0x21}, 1, 0},
    {0x8A, (uint8_t[]){0x00}, 1, 0},
    {0x8B, (uint8_t[]){0x80}, 1, 0},
    {0x8C, (uint8_t[]){0x01}, 1, 0},
    {0x8D, (uint8_t[]){0x01}, 1, 0},
    {0x8E, (uint8_t[]){0xFF}, 1, 0},
    {0x8F, (uint8_t[]){0xFF}, 1, 0},
    {0xB6, (uint8_t[]){0x00, 0x20}, 2, 0},
    {0x36, (uint8_t[]){0x08}, 1, 0},
    {0x3A, (uint8_t[]){0x05}, 1, 0},
    {0x90, (uint8_t[]){0x08, 0x08, 0x08, 0x08}, 4, 0},
    {0xBD, (uint8_t[]){0x06}, 1, 0},
    {0xBC, (uint8_t[]){0x00}, 1, 0},
    {0xFF, (uint8_t[]){0x60, 0x01, 0x04}, 3, 0},
    {0xC3, (uint8_t[]){0x13}, 1, 0},
    {0xC4, (uint8_t[]){0x13}, 1, 0},
    {0xC9, (uint8_t[]){0x22}, 1, 0},
    {0xBE, (uint8_t[]){0x11}, 1, 0},
    {0xE1, (uint8_t[]){0x10, 0x0E}, 2, 0},
    {0xDF, (uint8_t[]){0x21, 0x0C, 0x02}, 3, 0},
    {0xF0, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6, 0},
    {0xF1, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6, 0},
    {0xF2, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6, 0},
    {0xF3, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6, 0},
    {0xED, (uint8_t[]){0x1B, 0x0B}, 2, 0},
    {0xAE, (uint8_t[]){0x77}, 1, 0},
    {0xCD, (uint8_t[]){0x63}, 1, 0},
    {0x70, (uint8_t[]){0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}, 9, 0},
    {0xE8, (uint8_t[]){0x34}, 1, 0},
    {0x62, (uint8_t[]){0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}, 12, 0},
    {0x63, (uint8_t[]){0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}, 12, 0},
    {0x64, (uint8_t[]){0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}, 7, 0},
    {0x66, (uint8_t[]){0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10, 0},
    {0x67, (uint8_t[]){0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10, 0},
    {0x74, (uint8_t[]){0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}, 7, 0},
    {0x98, (uint8_t[]){0x3E, 0x07}, 2, 0},
    {0x35, nullptr, 0, 0},
    {0x21, nullptr, 0, 0},
    {0x11, nullptr, 0, 120},
    {0x29, nullptr, 0, 20},
};
#endif

}  // namespace

EspLcdGc9a01DisplayFlushBackend::EspLcdGc9a01DisplayFlushBackend()
    : ioHandle(nullptr),
      flushDoneSemaphore(nullptr),
      swapBuffer(nullptr),
      swapBufferPixels(0),
      initialized(false) {
}

EspLcdGc9a01DisplayFlushBackend::~EspLcdGc9a01DisplayFlushBackend() {
    if (ioHandle != nullptr) {
        esp_lcd_panel_io_del(ioHandle);
    }
    if (swapBuffer != nullptr) {
        heap_caps_free(swapBuffer);
    }
    if (flushDoneSemaphore != nullptr) {
        vSemaphoreDelete(flushDoneSemaphore);
    }
}

bool EspLcdGc9a01DisplayFlushBackend::initialize() {
    return ensureInitialized();
}

void EspLcdGc9a01DisplayFlushBackend::setBrightness(uint8_t brightness) {
#if !QNOB_DISPLAY_IS_QSPI
    if (LCD_BL_PIN >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(LCD_BL_PIN), brightness > 0 ? 1 : 0);
    }
#else
    (void)brightness;
#endif
}

void EspLcdGc9a01DisplayFlushBackend::setDisplayOn(bool enabled) {
#if !QNOB_DISPLAY_IS_QSPI
    if (!ensureInitialized()) {
        return;
    }
    txCommand(enabled ? 0x29 : 0x28);
#else
    (void)enabled;
#endif
}

int EspLcdGc9a01DisplayFlushBackend::width() const {
    return DISPLAY_WIDTH;
}

int EspLcdGc9a01DisplayFlushBackend::height() const {
    return DISPLAY_HEIGHT;
}

uint32_t EspLcdGc9a01DisplayFlushBackend::preferredDrawBufferLines() const {
    return 40;
}

uint32_t EspLcdGc9a01DisplayFlushBackend::minimumDrawBufferLines() const {
    return 10;
}

bool EspLcdGc9a01DisplayFlushBackend::requiresEvenAreaAlignment() const {
    return false;
}

void EspLcdGc9a01DisplayFlushBackend::beginWrite() {
    ensureInitialized();
}

void EspLcdGc9a01DisplayFlushBackend::writeImage(int32_t x,
                                                 int32_t y,
                                                 int32_t widthPixels,
                                                 int32_t heightPixels,
                                                 const void* pixelData,
                                                 DisplayFlushFormat format) {
#if !QNOB_DISPLAY_IS_QSPI
    if (!ensureInitialized()) {
        return;
    }

    const size_t pixelCount = static_cast<size_t>(widthPixels) * static_cast<size_t>(heightPixels);
    const void* preparedPixelData = preparePixelData(pixelData, pixelCount, format);
    if (preparedPixelData == nullptr) {
        ESP_LOGE(TAG, "Failed to prepare pixel data for %ldx%ld flush", widthPixels, heightPixels);
        return;
    }

    if (!setAddressWindow(x, y, widthPixels, heightPixels)) {
        return;
    }

    if (xSemaphoreTake(flushDoneSemaphore, 0) == pdTRUE) {
        // Drain stale completion tokens before submitting a new color transfer.
    }

    esp_err_t err = esp_lcd_panel_io_tx_color(ioHandle, 0x2C, preparedPixelData, pixelCount * sizeof(uint16_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GC9A01 color transfer failed: %s", esp_err_to_name(err));
        return;
    }

    if (xSemaphoreTake(flushDoneSemaphore, kFlushTimeoutTicks) != pdTRUE) {
        ESP_LOGE(TAG, "Timed out waiting for GC9A01 flush completion");
    }
#else
    (void)x;
    (void)y;
    (void)widthPixels;
    (void)heightPixels;
    (void)pixelData;
    (void)format;
#endif
}

void EspLcdGc9a01DisplayFlushBackend::endWrite() {
}

bool EspLcdGc9a01DisplayFlushBackend::ensureInitialized() {
#if !QNOB_DISPLAY_IS_QSPI
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

    if (LCD_BL_PIN >= 0) {
        gpio_config_t backlightConfig = {};
        backlightConfig.pin_bit_mask = 1ULL << LCD_BL_PIN;
        backlightConfig.mode = GPIO_MODE_OUTPUT;
        gpio_config(&backlightConfig);
        gpio_set_level(static_cast<gpio_num_t>(LCD_BL_PIN), 0);
    }

    if (LCD_RST_PIN >= 0) {
        gpio_config_t resetConfig = {};
        resetConfig.pin_bit_mask = 1ULL << LCD_RST_PIN;
        resetConfig.mode = GPIO_MODE_OUTPUT;
        gpio_config(&resetConfig);
        gpio_set_level(static_cast<gpio_num_t>(LCD_RST_PIN), 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(static_cast<gpio_num_t>(LCD_RST_PIN), 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    spi_bus_config_t busConfig = {};
    busConfig.sclk_io_num = LCD_CLK_PIN;
    busConfig.mosi_io_num = LCD_MOSI_PIN;
    busConfig.miso_io_num = LCD_MISO_PIN;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    busConfig.max_transfer_sz = kMaxTransferBytes;
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus for GC9A01: %s", esp_err_to_name(err));
        return false;
    }

    if (ioHandle == nullptr) {
        esp_lcd_panel_io_spi_config_t ioConfig = {};
        ioConfig.cs_gpio_num = LCD_CS_PIN;
        ioConfig.dc_gpio_num = LCD_DC_PIN;
        ioConfig.spi_mode = 0;
        ioConfig.pclk_hz = 40 * 1000 * 1000;
        ioConfig.trans_queue_depth = 10;
        ioConfig.on_color_trans_done = notifyFlushDone;
        ioConfig.user_ctx = this;
        ioConfig.lcd_cmd_bits = 8;
        ioConfig.lcd_param_bits = 8;
        err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                       &ioConfig,
                                       &ioHandle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create GC9A01 panel IO: %s", esp_err_to_name(err));
            return false;
        }
    }

    for (const auto& initCmd : kPanelInitCmds) {
        if (!txCommand(initCmd.command, initCmd.data, initCmd.dataLength)) {
            return false;
        }
        if (initCmd.delayMs > 0) {
            vTaskDelay(pdMS_TO_TICKS(initCmd.delayMs));
        }
    }

    initialized = true;
    ESP_LOGI(TAG, "Initialized esp_lcd SPI GC9A01 backend for %s", QNOB_BOARD_NAME);
    return true;
#else
    ESP_LOGE(TAG, "GC9A01 backend selected for non-LCD board %s", QNOB_BOARD_NAME);
    return false;
#endif
}

bool EspLcdGc9a01DisplayFlushBackend::ensureSwapBuffer(size_t pixelCount) {
    if (pixelCount <= swapBufferPixels && swapBuffer != nullptr) {
        return true;
    }

    uint16_t* newBuffer = static_cast<uint16_t*>(
        heap_caps_malloc(pixelCount * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (newBuffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate swap buffer for %u pixels", static_cast<unsigned>(pixelCount));
        return false;
    }

    if (swapBuffer != nullptr) {
        heap_caps_free(swapBuffer);
    }

    swapBuffer = newBuffer;
    swapBufferPixels = pixelCount;
    return true;
}

const void* EspLcdGc9a01DisplayFlushBackend::preparePixelData(const void* pixelData,
                                                              size_t pixelCount,
                                                              DisplayFlushFormat format) {
    if (pixelData == nullptr) {
        return nullptr;
    }

    // GC9A01 receives RGB565 as high byte then low byte over SPI.
    if (format == DisplayFlushFormat::RGB565_SWAPPED) {
        return pixelData;
    }

    if (!ensureSwapBuffer(pixelCount)) {
        return nullptr;
    }

    const uint16_t* source = static_cast<const uint16_t*>(pixelData);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint16_t color = source[i];
        swapBuffer[i] = static_cast<uint16_t>((color << 8) | (color >> 8));
    }

    return swapBuffer;
}

bool EspLcdGc9a01DisplayFlushBackend::txCommand(uint8_t command, const void* data, size_t dataLength) {
#if !QNOB_DISPLAY_IS_QSPI
    esp_err_t err = esp_lcd_panel_io_tx_param(ioHandle, command, data, dataLength);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GC9A01 command 0x%02x failed: %s", command, esp_err_to_name(err));
        return false;
    }
    return true;
#else
    (void)command;
    (void)data;
    (void)dataLength;
    return false;
#endif
}

bool EspLcdGc9a01DisplayFlushBackend::setAddressWindow(int32_t x,
                                                       int32_t y,
                                                       int32_t widthPixels,
                                                       int32_t heightPixels) {
#if !QNOB_DISPLAY_IS_QSPI
    const uint16_t xStart = static_cast<uint16_t>(x + DISPLAY_OFFSET_X);
    const uint16_t yStart = static_cast<uint16_t>(y + DISPLAY_OFFSET_Y);
    const uint16_t xEnd = static_cast<uint16_t>(xStart + widthPixels - 1);
    const uint16_t yEnd = static_cast<uint16_t>(yStart + heightPixels - 1);

    uint8_t columnData[] = {
        static_cast<uint8_t>(xStart >> 8),
        static_cast<uint8_t>(xStart & 0xFF),
        static_cast<uint8_t>(xEnd >> 8),
        static_cast<uint8_t>(xEnd & 0xFF),
    };
    uint8_t rowData[] = {
        static_cast<uint8_t>(yStart >> 8),
        static_cast<uint8_t>(yStart & 0xFF),
        static_cast<uint8_t>(yEnd >> 8),
        static_cast<uint8_t>(yEnd & 0xFF),
    };

    return txCommand(0x2A, columnData, sizeof(columnData)) &&
           txCommand(0x2B, rowData, sizeof(rowData));
#else
    (void)x;
    (void)y;
    (void)widthPixels;
    (void)heightPixels;
    return false;
#endif
}

bool EspLcdGc9a01DisplayFlushBackend::notifyFlushDone(esp_lcd_panel_io_handle_t panelIo,
                                                      esp_lcd_panel_io_event_data_t* eventData,
                                                      void* userCtx) {
    (void)panelIo;
    (void)eventData;

    BaseType_t highTaskWoken = pdFALSE;
    auto* self = static_cast<EspLcdGc9a01DisplayFlushBackend*>(userCtx);
    if (self != nullptr && self->flushDoneSemaphore != nullptr) {
        xSemaphoreGiveFromISR(self->flushDoneSemaphore, &highTaskWoken);
    }
    return highTaskWoken == pdTRUE;
}
