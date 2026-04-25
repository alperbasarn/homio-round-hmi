/*----------------------------------------------------------------------------/
 *  Lovyan GFX - Graphics library for embedded devices.
 *
 * Original Source:
 * https://github.com/lovyan03/LovyanGFX/
 *
 * Licence:
 * [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)
 *
 * Author:
 * [lovyan03](https://twitter.com/lovyan03)
 *
 * Contributors:
 * [ciniml](https://github.com/ciniml)
 * [mongonta0716](https://github.com/mongonta0716)
 * [tobozo](https://github.com/tobozo)
 * /----------------------------------------------------------------------------*/
#pragma once

#if defined (ESP_PLATFORM)

#include "Panel_AMOLED.hpp"
#include "Panel_FrameBufferBase.hpp"
#include "../platforms/common.hpp"
#include "../platforms/device.hpp"

#if defined LGFX_USE_QSPI

namespace lgfx
{
    inline namespace v1
    {
        //----------------------------------------------------------------------------

        // Panel used by LilyGO T-Watch-Ultra-AMOLED

        struct Panel_CO5300 : public Panel_AMOLED
        {
        public:

            Panel_CO5300(void)
            {
              _cfg.memory_width  = _cfg.panel_width  = 466;
              _cfg.memory_height = _cfg.panel_height = 466;
              _write_depth = color_depth_t::rgb565_2Byte;
              _read_depth = color_depth_t::rgb565_2Byte;
              _cfg.dummy_read_pixel = 1;
            }

            const uint8_t* getInitCommands(uint8_t listno) const override
            {
              static constexpr uint8_t list0[] = {
                0xFE, 1, 0x20,
                0x19, 1, 0x10,
                0x1C, 1, 0xA0,
                0xFE, 1, 0x00,
                0xC4, 1, 0x80,
                0x3A, 1, 0x55,
                0x35, 1, 0x00,
                0x53, 1, 0x20,
                0x51, 1, 0xFF,
                0x63, 1, 0xFF,
                0x2A, 4, 0x00, 0x06, 0x01, 0xD7,
                0x2B, 4, 0x00, 0x00, 0x01, 0xD1,
                0x11, 0x80, 0, // Sleep out
                0x29, 0x80, 0, // display on
                0xff, 0xff // end
              };
              switch (listno) {
                case 0: return list0;
                default: return nullptr;
              }
            }
        };

        //----------------------------------------------------------------------------
    }

}

#endif
#endif
