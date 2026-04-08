# QNOB Screen - ESP-IDF Project

ESP-IDF based firmware for the QNOB smart knob display, supporting both ESP32-S3 and ESP32-C6 targets.

## Supported Hardware

| Target | Display | Resolution | Board |
|--------|---------|------------|-------|
| ESP32-S3 | GC9A01 LCD | 240x240 | Waveshare ESP32-S3-Touch-LCD-1.28 |
| ESP32-C6 | RM67162 AMOLED | 466x466 | Waveshare ESP32-C6-Touch-AMOLED-1.43 |

## Project Structure

```
esp-idf/
├── main/                     # Main application
│   ├── main.cpp              # Entry point
│   ├── CMakeLists.txt
│   └── Kconfig.projbuild     # App configuration options
├── components/
│   ├── hal/                  # Hardware Abstraction Layer
│   │   ├── hal_config.h      # Pin definitions for all targets
│   │   ├── hal_init.c/h      # HAL initialization
│   │   ├── Kconfig           # Hardware configuration options
│   │   └── CMakeLists.txt
│   ├── display/              # Display controller
│   │   ├── LGFX_Config.hpp   # LovyanGFX configuration
│   │   ├── DisplayController.cpp/h
│   │   └── CMakeLists.txt
│   ├── touch_panel/          # CST816S touch controller
│   │   ├── TouchPanel.cpp/h
│   │   └── CMakeLists.txt
│   ├── knob_controller/      # UART communication with knob
│   │   ├── KnobController.cpp/h
│   │   └── CMakeLists.txt
│   ├── storage/              # NVS-based configuration storage
│   │   ├── NVSManager.cpp/h
│   │   └── CMakeLists.txt
│   └── LovyanGFX/            # Graphics library (submodule)
├── sdkconfig.defaults        # Common configuration
├── sdkconfig.defaults.esp32s3  # ESP32-S3 specific config
├── sdkconfig.defaults.esp32c6  # ESP32-C6 specific config
├── partitions.csv            # Partition table
└── CMakeLists.txt            # Root CMake file
```

## Building

### Prerequisites

1. Install ESP-IDF v5.x: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/
2. Set up environment: `. $IDF_PATH/export.sh` (Linux/macOS) or `export.bat` (Windows)

### Build for ESP32-S3

```bash
cd screen/SW/esp-idf
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Build for ESP32-C6

```bash
cd screen/SW/esp-idf
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Configuration

Use `idf.py menuconfig` to configure:

- **QNOB Hardware Configuration**: Select target hardware, enable/disable features
- **QNOB Application Configuration**: Set device name, default WiFi credentials, MQTT settings

## Components

### HAL (Hardware Abstraction Layer)
Provides unified pin definitions and hardware initialization across different targets.
Uses `CONFIG_IDF_TARGET_*` macros for compile-time target selection.

### TouchPanel
ESP-IDF I2C driver for CST816S capacitive touch controller.
Supports press, release, hold, and gesture detection.

### KnobController
ESP-IDF UART driver for communication with external knob controller.
Handles serial protocol for setpoint and command exchange.

### Storage (NVSManager)
Non-Volatile Storage based configuration management.
Replaces Arduino EEPROM with ESP-IDF NVS for persistent settings.

### Display
LovyanGFX-based display driver supporting both GC9A01 (LCD) and RM67162 (AMOLED).

## Migration from Arduino

This project is a port of the Arduino-based QNOB firmware. Key changes:

| Arduino | ESP-IDF |
|---------|---------|
| `Arduino_GFX_Library` | LovyanGFX |
| `EEPROM.h` | `nvs_flash.h` |
| `SoftwareSerial` | `driver/uart.h` |
| `WiFi.h` | `esp_wifi.h` |
| `PubSubClient` | `esp_mqtt_client.h` |
| `delay()` | `vTaskDelay()` |
| `millis()` | `esp_timer_get_time()` |

## License

See LICENSE file in root directory.
