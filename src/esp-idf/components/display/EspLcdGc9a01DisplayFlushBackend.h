#pragma once

#include "DisplayFlushBackend.h"

#include "esp_lcd_panel_io.h"
#include "freertos/semphr.h"

#include <cstddef>
#include <cstdint>

class EspLcdGc9a01DisplayFlushBackend : public DisplayFlushBackend {
public:
    EspLcdGc9a01DisplayFlushBackend();
    ~EspLcdGc9a01DisplayFlushBackend() override;

    bool initialize();
    void setBrightness(uint8_t brightness);
    void setDisplayOn(bool enabled);

    int width() const override;
    int height() const override;
    uint32_t preferredDrawBufferLines() const override;
    uint32_t minimumDrawBufferLines() const override;
    bool requiresEvenAreaAlignment() const override;
    void beginWrite() override;
    void writeImage(int32_t x,
                    int32_t y,
                    int32_t width,
                    int32_t height,
                    const void* pixelData,
                    DisplayFlushFormat format) override;
    void endWrite() override;

private:
    bool ensureInitialized();
    bool ensureSwapBuffer(size_t pixelCount);
    const void* preparePixelData(const void* pixelData, size_t pixelCount, DisplayFlushFormat format);
    bool txCommand(uint8_t command, const void* data = nullptr, size_t dataLength = 0);
    bool setAddressWindow(int32_t x, int32_t y, int32_t width, int32_t height);
    static bool notifyFlushDone(esp_lcd_panel_io_handle_t panelIo,
                                esp_lcd_panel_io_event_data_t* eventData,
                                void* userCtx);

    esp_lcd_panel_io_handle_t ioHandle;
    SemaphoreHandle_t flushDoneSemaphore;
    uint16_t* swapBuffer;
    size_t swapBufferPixels;
    bool initialized;
};
