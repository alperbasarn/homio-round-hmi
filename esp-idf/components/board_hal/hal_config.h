#pragma once

#include "sdkconfig.h"

// Touch controller families
#define QNOB_TOUCH_CONTROLLER_UNKNOWN 0
#define QNOB_TOUCH_CONTROLLER_CST816S 1
#define QNOB_TOUCH_CONTROLLER_FT6X36  2
#define QNOB_TOUCH_CONTROLLER_CST9217 3

// Battery telemetry sources
#define QNOB_BATTERY_SOURCE_NONE     0
#define QNOB_BATTERY_SOURCE_ADC      1
#define QNOB_BATTERY_SOURCE_AXP2101  2

// Audio output/input backends
#define QNOB_AUDIO_OUTPUT_BACKEND_NONE 0
#define QNOB_AUDIO_OUTPUT_BACKEND_I2S  1

#define QNOB_AUDIO_INPUT_BACKEND_NONE  0
#define QNOB_AUDIO_INPUT_BACKEND_I2S   1

// Audio codec backends
#define QNOB_AUDIO_CODEC_NONE          0
#define QNOB_AUDIO_CODEC_ES8311        1

// Audio amplifier control backends
#define QNOB_AUDIO_AMP_BACKEND_NONE    0
#define QNOB_AUDIO_AMP_BACKEND_GPIO    1
#define QNOB_AUDIO_AMP_BACKEND_EXIO    2

// Touch panel I2C addresses
#define CST816S_I2C_ADDR        0x15
#define FT6X36_I2C_ADDR         0x38
#define CST9217_I2C_ADDR        0x5A

// ######################################################################################################################
// #                                           ESP32-S3 LCD 1.28                                                        #
// ######################################################################################################################
#if CONFIG_QNOB_HW_ESP32S3_LCD_128

#define QNOB_BOARD_NAME         "ESP32-S3 Touch LCD 1.28"
#define QNOB_OTA_VARIANT_ID     "esp32s3_lcd128"
#define QNOB_DISPLAY_IS_QSPI    0
#define QNOB_TOUCH_CONTROLLER   QNOB_TOUCH_CONTROLLER_CST816S
#define QNOB_BATTERY_SOURCE     QNOB_BATTERY_SOURCE_ADC

// ========== Display (GC9A01 240x240 LCD) ===========
#define LCD_CLK_PIN             10
#define LCD_MOSI_PIN            11
#define LCD_MISO_PIN            -1
#define LCD_D2_PIN              -1
#define LCD_D3_PIN              -1
#define LCD_CS_PIN              9
#define LCD_DC_PIN              8
#define LCD_RST_PIN             14
#define LCD_BL_PIN              2

// Display properties
#define DISPLAY_WIDTH           240
#define DISPLAY_HEIGHT          240
#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        0

// ========== Touch Panel (CST816S) ===========
#define TP_SDA_PIN              6
#define TP_SCL_PIN              7
#define TP_INT_PIN              5
#define TP_RST_PIN              13
#define TP_I2C_ADDR             CST816S_I2C_ADDR
#define TOUCH_WAKEUP_PIN        5
#define TOUCH_SWAP_XY           0
#define TOUCH_MIRROR_X          0
#define TOUCH_MIRROR_Y          0

// ========== IMU (QMI8658) ===========
#define IMU_SDA_PIN             6
#define IMU_SCL_PIN             7
#define IMU_INT1_PIN            3
#define IMU_INT2_PIN            4

// ========== UART for Knob Communication ===========
#define KNOB_UART_NUM           UART_NUM_1
#define KNOB_TX_PIN             17
#define KNOB_RX_PIN             18
#define KNOB_BAUD_RATE          9600

// ========== Battery ADC ===========
#define BAT_ADC_PIN             1

// ========== SD Card (SPI Mode) ===========
#define SD_SPI_HOST             SPI2_HOST
#define SD_MOSI_PIN             11
#define SD_MISO_PIN             12
#define SD_CLK_PIN              10
#define SD_CS_PIN               15

// ========== Audio (I2S + Codec) ===========
#define QNOB_AUDIO_OUTPUT_BACKEND QNOB_AUDIO_OUTPUT_BACKEND_NONE
#define QNOB_AUDIO_INPUT_BACKEND  QNOB_AUDIO_INPUT_BACKEND_NONE
#define QNOB_AUDIO_CODEC          QNOB_AUDIO_CODEC_NONE
#define QNOB_AUDIO_AMP_BACKEND    QNOB_AUDIO_AMP_BACKEND_NONE
#define AUDIO_I2S_PORT          I2S_NUM_0
#define AUDIO_I2S_BCLK_PIN      -1
#define AUDIO_I2S_WS_PIN        -1
#define AUDIO_I2S_DOUT_PIN      -1
#define AUDIO_I2S_DIN_PIN       -1
#define AUDIO_I2S_MCLK_PIN      -1
#define AUDIO_CODEC_I2C_ADDR    -1
#define AUDIO_PA_EN_GPIO_PIN    -1
#define AUDIO_PA_EN_EXIO_BIT    -1

// ========== I2C Configuration ===========
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   1000

// ========== FreeRTOS Core Assignment ===========
#define DISPLAY_CORE            0
#define NETWORK_CORE            1
#define DUAL_CORE_AVAILABLE     1

// ######################################################################################################################
// #                                         ESP32-S3 AMOLED 1.75                                                       #
// ######################################################################################################################
#elif CONFIG_QNOB_HW_ESP32S3_AMOLED_175

#define QNOB_BOARD_NAME         "ESP32-S3 Touch AMOLED 1.75"
#define QNOB_OTA_VARIANT_ID     "esp32s3_amoled175"
#define QNOB_DISPLAY_IS_QSPI    1
#define QNOB_TOUCH_CONTROLLER   QNOB_TOUCH_CONTROLLER_CST9217
#define QNOB_BATTERY_SOURCE     QNOB_BATTERY_SOURCE_AXP2101

// ========== Display (CO5300 466x466 AMOLED, QSPI) ===========
// Based on Waveshare's official BSP for the ESP32-S3-Touch-AMOLED-1.75 board.
#define LCD_CLK_PIN             38
#define LCD_MOSI_PIN            4
#define LCD_MISO_PIN            5
#define LCD_D2_PIN              6
#define LCD_D3_PIN              7
#define LCD_CS_PIN              12
#define LCD_DC_PIN              -1
#define LCD_RST_PIN             39
#define LCD_BL_PIN              -1

// Display properties
#define DISPLAY_WIDTH           466
#define DISPLAY_HEIGHT          466
#define DISPLAY_OFFSET_X        6
#define DISPLAY_OFFSET_Y        0

// ========== Touch Panel (CST9217) ===========
#define TP_SDA_PIN              15
#define TP_SCL_PIN              14
#define TP_INT_PIN              11
#define TP_RST_PIN              40
#define TP_I2C_ADDR             CST9217_I2C_ADDR
#define TOUCH_WAKEUP_PIN        11
// S3 AMOLED 1.75 reports coordinates opposite to display orientation.
#define TOUCH_SWAP_XY           0
#define TOUCH_MIRROR_X          1
#define TOUCH_MIRROR_Y          1

// ========== Shared I2C Devices ===========
#define IMU_SDA_PIN             15
#define IMU_SCL_PIN             14
#define IMU_INT1_PIN            -1
#define IMU_INT2_PIN            -1
#define EXIO_I2C_ADDR           0x20
#define PMU_I2C_ADDR            0x34
#define RTC_I2C_ADDR            0x51

// ========== UART for Knob Communication ===========
// The Waveshare BSP does not expose a canonical UART mapping for the 8-pin header yet.
// Leave the external knob UART disabled until the header pins are confirmed.
#define KNOB_UART_NUM           UART_NUM_1
#define KNOB_TX_PIN             -1
#define KNOB_RX_PIN             -1
#define KNOB_BAUD_RATE          9600

// ========== Battery / Power ===========
// Battery telemetry is handled by the onboard AXP2101 PMU rather than a simple ADC divider.
#define BAT_ADC_PIN             -1
#define PWR_KEY_PIN             -1

// ========== SD Card (SPI Mode) ===========
// Official Waveshare wiki Arduino example:
// CS=GPIO41, MOSI=GPIO1, MISO=GPIO3, SCLK=GPIO2
#define SD_SPI_HOST             SPI2_HOST
#define SD_MOSI_PIN             1
#define SD_MISO_PIN             3
#define SD_CLK_PIN              2
#define SD_CS_PIN               41

// ========== Audio (I2S + Codec) ===========
#define QNOB_AUDIO_OUTPUT_BACKEND QNOB_AUDIO_OUTPUT_BACKEND_I2S
#define QNOB_AUDIO_INPUT_BACKEND  QNOB_AUDIO_INPUT_BACKEND_I2S
#define QNOB_AUDIO_CODEC          QNOB_AUDIO_CODEC_ES8311
#define QNOB_AUDIO_AMP_BACKEND    QNOB_AUDIO_AMP_BACKEND_GPIO
#define AUDIO_I2S_PORT          I2S_NUM_0
#define AUDIO_I2S_BCLK_PIN      9
#define AUDIO_I2S_WS_PIN        45
#define AUDIO_I2S_DOUT_PIN      8
#define AUDIO_I2S_DIN_PIN       10
#define AUDIO_I2S_MCLK_PIN      42
#define AUDIO_CODEC_I2C_ADDR    0x18
#define AUDIO_PA_EN_GPIO_PIN    46
#define AUDIO_PA_EN_EXIO_BIT    -1

// ========== I2C Configuration ===========
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   1000

// ========== FreeRTOS Core Assignment ===========
#define DISPLAY_CORE            0
#define NETWORK_CORE            1
#define DUAL_CORE_AVAILABLE     1

// ######################################################################################################################
// #                                           ESP32-C6 AMOLED 1.43                                                     #
// ######################################################################################################################
#elif CONFIG_QNOB_HW_ESP32C6_AMOLED_143

#define QNOB_BOARD_NAME         "ESP32-C6 Touch AMOLED 1.43"
#define QNOB_OTA_VARIANT_ID     "esp32c6_amoled143"
#define QNOB_DISPLAY_IS_QSPI    1
#define QNOB_TOUCH_CONTROLLER   QNOB_TOUCH_CONTROLLER_FT6X36
#define QNOB_BATTERY_SOURCE     QNOB_BATTERY_SOURCE_ADC

// ========== Display (1.43" AMOLED module, SH8601 QSPI controller) ===========
// Waveshare ESP32-C6-Touch-AMOLED-1.43 schematic mapping:
// D0=IO4, D1=IO5, D2=IO6, D3=IO7, CLK=IO11, CS=IO10, RST=IO3
#define LCD_CLK_PIN             11
#define LCD_MOSI_PIN            4
#define LCD_MISO_PIN            5
#define LCD_D2_PIN              6
#define LCD_D3_PIN              7
#define LCD_CS_PIN              10
#define LCD_DC_PIN              -1
#define LCD_RST_PIN             3
#define LCD_BL_PIN              -1

// Display properties
#define DISPLAY_WIDTH           466
#define DISPLAY_HEIGHT          466
#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        0

// ========== Touch Panel ===========
// Waveshare's C6 AMOLED boards use an FT6x36-compatible touch controller
// (often documented as FT6146) on the shared 0x38 I2C bus.
#define TP_SDA_PIN              18
#define TP_SCL_PIN              8
#define TP_INT_PIN              -1
#define TP_RST_PIN              -1
#define TP_I2C_ADDR             FT6X36_I2C_ADDR
#define TOUCH_WAKEUP_PIN        1
#define TOUCH_SWAP_XY           0
#define TOUCH_MIRROR_X          0
#define TOUCH_MIRROR_Y          0

// ========== IMU (QMI8658) ===========
#define IMU_SDA_PIN             6
#define IMU_SCL_PIN             7
#define IMU_INT1_PIN            4
#define IMU_INT2_PIN            5

// ========== UART for Knob Communication (via J16 on ScreenBase_V2) ===========
#define KNOB_UART_NUM           UART_NUM_1
#define KNOB_TX_PIN             2
#define KNOB_RX_PIN             1
#define KNOB_BAUD_RATE          9600

// ========== Battery ADC ===========
#define BAT_ADC_PIN             0

// ========== SD Card (SPI Mode) ===========
// Waveshare C6 AMOLED schematic and official examples:
// SD CLK=IO11, SD CS=IO15, SD DI(MOSI)=IO4, SD DO(MISO)=IO5
#define SD_SPI_HOST             SPI2_HOST
#define SD_MOSI_PIN             4
#define SD_MISO_PIN             5
#define SD_CLK_PIN              11
#define SD_CS_PIN               15

// ========== Audio (I2S + Codec) ===========
// Official board audio config:
// i2s: bclk=IO21, ws=IO22, dout=IO23, din=IO20, mclk=IO19
#define QNOB_AUDIO_OUTPUT_BACKEND QNOB_AUDIO_OUTPUT_BACKEND_I2S
#define QNOB_AUDIO_INPUT_BACKEND  QNOB_AUDIO_INPUT_BACKEND_I2S
#define QNOB_AUDIO_CODEC          QNOB_AUDIO_CODEC_ES8311
#define QNOB_AUDIO_AMP_BACKEND    QNOB_AUDIO_AMP_BACKEND_EXIO
#define AUDIO_I2S_PORT          I2S_NUM_0
#define AUDIO_I2S_BCLK_PIN      21
#define AUDIO_I2S_WS_PIN        22
#define AUDIO_I2S_DOUT_PIN      23
#define AUDIO_I2S_DIN_PIN       20
#define AUDIO_I2S_MCLK_PIN      19
#define AUDIO_CODEC_I2C_ADDR    0x18
#define AUDIO_PA_EN_GPIO_PIN    -1
#define AUDIO_PA_EN_EXIO_BIT    7

// ========== I2C Configuration ===========
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   1000

// ========== Power Management (TCA9554 I2C GPIO Expander) ===========
// Power latch requires setting EXIO6 HIGH to keep board powered on battery
#define EXIO_I2C_ADDR           0x20
#define EXIO_SDA_PIN            18
#define EXIO_SCL_PIN            8
#define EXIO_BAT_EN_BIT         6
#define EXIO_INT_PIN            1
#define EXIO_TOUCH_INT_BIT      0
#define PWR_KEY_PIN             2

// ========== FreeRTOS Core Assignment ===========
#define DISPLAY_CORE            0
#define NETWORK_CORE            0
#define DUAL_CORE_AVAILABLE     0

#else
#error "Unsupported target board. Select a QNOB hardware platform in menuconfig."
#endif

#ifndef PWR_KEY_PIN
#define PWR_KEY_PIN             -1
#endif

#ifndef PMU_I2C_ADDR
#define PMU_I2C_ADDR            -1
#endif

#ifndef QNOB_AUDIO_OUTPUT_BACKEND
#define QNOB_AUDIO_OUTPUT_BACKEND QNOB_AUDIO_OUTPUT_BACKEND_NONE
#endif

#ifndef QNOB_AUDIO_INPUT_BACKEND
#define QNOB_AUDIO_INPUT_BACKEND QNOB_AUDIO_INPUT_BACKEND_NONE
#endif

#ifndef QNOB_AUDIO_CODEC
#define QNOB_AUDIO_CODEC        QNOB_AUDIO_CODEC_NONE
#endif

#ifndef QNOB_AUDIO_AMP_BACKEND
#define QNOB_AUDIO_AMP_BACKEND  QNOB_AUDIO_AMP_BACKEND_NONE
#endif

// Task Priorities
#define DISPLAY_TASK_PRIORITY   2
#define NETWORK_TASK_PRIORITY   3
#define INIT_TASK_PRIORITY      4

// Task Stack Sizes
#define DISPLAY_STACK_SIZE      8192
#define NETWORK_STACK_SIZE      8192

// Timeouts
#define MQTT_CONNECT_TIMEOUT_MS     5000
#define INACTIVITY_TIMEOUT_MS       30000

// NVS Namespace
#define NVS_NAMESPACE           "qnob_config"
