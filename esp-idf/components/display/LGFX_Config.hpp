#pragma once

#if CONFIG_IDF_TARGET_ESP32C6
#define LGFX_USE_QSPI
#endif
#define LGFX_USE_V1

#include <LovyanGFX.hpp>
#include "hal_config.h"

class LGFX : public lgfx::LGFX_Device
{
#if CONFIG_IDF_TARGET_ESP32S3
  lgfx::Panel_GC9A01      _panel_instance;
  lgfx::Bus_SPI           _bus_instance;
#elif CONFIG_IDF_TARGET_ESP32C6
  lgfx::Panel_SH8601Z     _panel_instance;
  lgfx::Bus_SPI           _bus_instance;
#endif

public:
  LGFX(void)
  {
    // Configure SPI bus
    {
      auto cfg = _bus_instance.config();

      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.use_lock = true;
      cfg.pin_sclk = LCD_CLK_PIN;
      cfg.pin_mosi = LCD_MOSI_PIN;
      cfg.pin_miso = LCD_MISO_PIN;
      cfg.pin_dc   = LCD_DC_PIN;

#if CONFIG_IDF_TARGET_ESP32S3
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
#elif CONFIG_IDF_TARGET_ESP32C6
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = false;
      cfg.pin_io0    = LCD_MOSI_PIN;
      cfg.pin_io1    = LCD_MISO_PIN;
      cfg.pin_io2    = LCD_D2_PIN;
      cfg.pin_io3    = LCD_D3_PIN;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
#endif

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    // Configure panel
    {
      auto cfg = _panel_instance.config();

      cfg.pin_cs           = LCD_CS_PIN;
      cfg.pin_rst          = LCD_RST_PIN;
      cfg.pin_busy         = -1;

      cfg.memory_width     = DISPLAY_WIDTH;
      cfg.memory_height    = DISPLAY_HEIGHT;
      cfg.panel_width      = DISPLAY_WIDTH;
      cfg.panel_height     = DISPLAY_HEIGHT;
      cfg.offset_x         = DISPLAY_OFFSET_X;
      cfg.offset_y         = DISPLAY_OFFSET_Y;

      cfg.offset_rotation  = 0;
      cfg.bus_shared       = true;

#if CONFIG_IDF_TARGET_ESP32S3
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = true;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
#elif CONFIG_IDF_TARGET_ESP32C6
      cfg.readable         = false;
#endif

      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }

  // Helper to get display dimensions
  static constexpr int width() { return DISPLAY_WIDTH; }
  static constexpr int height() { return DISPLAY_HEIGHT; }
};
