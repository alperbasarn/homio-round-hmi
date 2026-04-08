#pragma once

#include "sdkconfig.h"

// ######################################################################################################################
// #                                           ESP32-S3 Configuration                                                    #
// ######################################################################################################################
#if CONFIG_IDF_TARGET_ESP32S3

// ========== Display (GC9A01 240x240 LCD) ===========
#define LCD_CLK_PIN     10
#define LCD_MOSI_PIN    11
#define LCD_MISO_PIN    -1
#define LCD_CS_PIN      9
#define LCD_DC_PIN      8
#define LCD_RST_PIN     14
#define LCD_BL_PIN      2

// Display properties
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

// ========== Touch Panel (CST816S) ===========
#define TP_SDA_PIN      6
#define TP_SCL_PIN      7
#define TP_INT_PIN      5
#define TP_RST_PIN      13

// ========== IMU (QMI8658) ===========
#define IMU_SDA_PIN     6   // Shared with Touch Panel
#define IMU_SCL_PIN     7   // Shared with Touch Panel
#define IMU_INT1_PIN    3
#define IMU_INT2_PIN    4

// ========== UART for Knob Communication ===========
#define KNOB_UART_NUM   UART_NUM_1
#define KNOB_TX_PIN     17
#define KNOB_RX_PIN     18
#define KNOB_BAUD_RATE  9600

// ========== Battery ADC ===========
#define BAT_ADC_PIN     1

// ========== SD Card (SPI Mode) ===========
#define SD_SPI_HOST     SPI2_HOST
#define SD_MOSI_PIN     11
#define SD_MISO_PIN     12
#define SD_CLK_PIN      10
#define SD_CS_PIN       15

// ========== Audio (I2S + Codec) ===========
#define AUDIO_I2S_PORT          I2S_NUM_0
#define AUDIO_I2S_BCLK_PIN      -1
#define AUDIO_I2S_WS_PIN        -1
#define AUDIO_I2S_DOUT_PIN      -1
#define AUDIO_I2S_DIN_PIN       -1
#define AUDIO_I2S_MCLK_PIN      -1
#define AUDIO_CODEC_I2C_ADDR    0x18
#define AUDIO_PA_EN_EXIO_BIT    7

// ========== I2C Configuration ===========
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   1000

// ========== FreeRTOS Core Assignment ===========
#define DISPLAY_CORE    0
#define NETWORK_CORE    1
#define DUAL_CORE_AVAILABLE 1

// ######################################################################################################################
// #                                           ESP32-C6 Configuration                                                    #
// ######################################################################################################################
#elif CONFIG_IDF_TARGET_ESP32C6

// ========== Display (CO5300 466x466 AMOLED, QSPI) ===========
// Waveshare ESP32-C6-Touch-AMOLED-1.43 schematic mapping:
// D0=IO4, D1=IO5, D2=IO6, D3=IO7, CLK=IO11, CS=IO10, RST=IO3
#define LCD_CLK_PIN     11
#define LCD_MOSI_PIN    4   // QSPI D0
#define LCD_MISO_PIN    5   // QSPI D1
#define LCD_D2_PIN      6
#define LCD_D3_PIN      7
#define LCD_CS_PIN      10
#define LCD_DC_PIN      -1  // QSPI panel does not use DC
#define LCD_RST_PIN     3
#define LCD_BL_PIN      -1  // AMOLED doesn't use classic backlight pin

// Display properties
#define DISPLAY_WIDTH   466
#define DISPLAY_HEIGHT  466
// Keep panel offsets at 0 here; LVGL flush alignment handles controller constraints.
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

// ========== Touch Panel ===========
// Board touch IC is FT6146 (not CST816S). Keep these as placeholders until FT6146 driver is added.
#define TP_SDA_PIN      18
#define TP_SCL_PIN      8
#define TP_INT_PIN      -1
#define TP_RST_PIN      -1
#define TP_I2C_ADDR     0x38

// ========== IMU (QMI8658) ===========
#define IMU_SDA_PIN     6   // Shared with Touch Panel
#define IMU_SCL_PIN     7   // Shared with Touch Panel
#define IMU_INT1_PIN    4
#define IMU_INT2_PIN    5

// ========== UART for Knob Communication (via J16 on ScreenBase_V2) ===========
#define KNOB_UART_NUM   UART_NUM_1
#define KNOB_TX_PIN     2
#define KNOB_RX_PIN     1
#define KNOB_BAUD_RATE  9600

// ========== Battery ADC ===========
#define BAT_ADC_PIN     0

// ========== SD Card (SPI Mode) ===========
// Waveshare C6 AMOLED schematic and official examples:
// SD CLK=IO11, SD CS=IO15, SD DI(MOSI)=IO4, SD DO(MISO)=IO5
#define SD_SPI_HOST     SPI2_HOST
#define SD_MOSI_PIN     4
#define SD_MISO_PIN     5
#define SD_CLK_PIN      11
#define SD_CS_PIN       15

// ========== Audio (I2S + Codec) ===========
// Official board audio config:
// i2s: bclk=IO21, ws=IO22, dout=IO23, din=IO20, mclk=IO19
#define AUDIO_I2S_PORT          I2S_NUM_0
#define AUDIO_I2S_BCLK_PIN      21
#define AUDIO_I2S_WS_PIN        22
#define AUDIO_I2S_DOUT_PIN      23
#define AUDIO_I2S_DIN_PIN       20
#define AUDIO_I2S_MCLK_PIN      19
#define AUDIO_CODEC_I2C_ADDR    0x18
#define AUDIO_PA_EN_EXIO_BIT    7

// ========== I2C Configuration ===========
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   1000

// ========== Power Management (TCA9554 I2C GPIO Expander) ===========
// Power latch requires setting EXIO6 HIGH to keep board powered on battery
#define EXIO_I2C_ADDR       0x20    // TCA9554 address (A0=A1=A2=GND)
#define EXIO_SDA_PIN        18      // Same as touch panel
#define EXIO_SCL_PIN        8       // Same as touch panel
#define EXIO_BAT_EN_BIT     6       // EXIO6 = BAT_EN (power latch), per official BATT_PWR test
#define EXIO_INT_PIN        1       // TCA9554 INT# output → GPIO1 (directly wired on PCB)
#define EXIO_TOUCH_INT_BIT  0       // EXIO0 = FT6146 touch INT (active LOW)
#define PWR_KEY_PIN         2       // GPIO2 = PWR_KEY (power button detect)

// ========== FreeRTOS Core Assignment ===========
// ESP32-C6 is single-core, so all tasks run on core 0
#define DISPLAY_CORE    0
#define NETWORK_CORE    0
#define DUAL_CORE_AVAILABLE 0

#else
#error "Unsupported target. Please use ESP32-S3 or ESP32-C6."
#endif

// ######################################################################################################################
// #                                           Common Definitions                                                        #
// ######################################################################################################################

// Touch Panel I2C Address
#define CST816S_I2C_ADDR        0x15
#ifndef TP_I2C_ADDR
#define TP_I2C_ADDR             CST816S_I2C_ADDR
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
