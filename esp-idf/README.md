# QNOB Screen - ESP-IDF Project

ESP-IDF firmware for the QNOB smart knob display, with board support for the Waveshare ESP32-S3 LCD and AMOLED targets plus the ESP32-C6 AMOLED target.

## Supported Hardware

| Target | Display | Resolution | Board |
|--------|---------|------------|-------|
| ESP32-S3 | GC9A01 LCD | 240x240 | Waveshare ESP32-S3-Touch-LCD-1.28 |
| ESP32-S3 | CO5300-compatible QSPI AMOLED | 466x466 | Waveshare ESP32-S3-Touch-AMOLED-1.75 |
| ESP32-C6 | SH8601-compatible QSPI AMOLED | 466x466 | Waveshare ESP32-C6-Touch-AMOLED-1.43 |

## Project Structure

```text
esp-idf/
|- main/                    # Main application
|- components/board_hal/    # Pin mappings and target-specific HAL init
|- components/display/      # LovyanGFX display integration
|- components/touch_panel/  # CST816S / CST9217 / FT6x36-compatible touch support
|- components/knob_controller/
|- components/storage/
|- sdkconfig.defaults
|- sdkconfig.defaults.esp32s3_lcd128
|- sdkconfig.defaults.esp32s3
|- sdkconfig.defaults.esp32s3_amoled175
|- sdkconfig.defaults.esp32c6
|- sdkconfig.defaults.esp32c6_amoled143
`- CMakeLists.txt
```

## Building

### Prerequisites

1. Install ESP-IDF v5.x: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/
2. Set up the ESP-IDF environment with `export.sh` or `export.bat`.

### Build with the repo scripts

From the repository root:

```bash
bash ./build-variant.sh esp32s3_lcd128
bash ./build-variant.sh esp32s3_amoled175
bash ./build-variant.sh esp32c6_amoled143
bash ./build-all.sh
```

Or from Windows PowerShell:

```powershell
.\build-variant.ps1 -Variant esp32s3_lcd128
.\build-variant.ps1 -Variant esp32s3_amoled175
.\build-variant.ps1 -Variant esp32c6_amoled143
.\build-all.ps1
```

The scripts reuse `~/esp/esp-idf/export.sh` and write generated `sdkconfig` files plus build outputs
under `Hardware Ref/`. Final flashable artifacts are copied to `../Binaries/<variant>/`.

Each build also publishes the OTA-safe application image into `../OTA/` using a board/version name:

- `S3-128-V<version>.bin`
- `S3-175-V<version>.bin`
- `C6-143-V<version>.bin`

These OTA files contain only the app image. Bootloader and partition-table changes still require a
full USB flash.

## OTA Workflow

The firmware already supports `otaUpdate`, `otaInfo`, and `otaStatus`. The desktop utility's
`Configure` tab can now serve the repo-root `OTA/` folder over local HTTP and trigger an update by
pointing the device at the selected versioned `.bin`.

Typical OTA flow:

1. Build a variant with `build-variant.sh` or `build-all.sh`
2. Open the desktop utility and go to `Configure`
3. Refresh the OTA file list, select a generated `S3-*` or `C6-*` image, and confirm the host/port
4. Click `Install Selected OTA`

The PC and the device need to be on the same network for OTA delivery.

### Manual build for ESP32-S3 LCD 1.28

```bash
cd esp-idf
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.esp32s3_lcd128" set-target esp32s3
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.esp32s3_lcd128" build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Build for ESP32-S3 AMOLED 1.75

```bash
cd esp-idf
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.esp32s3_amoled175" set-target esp32s3
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.esp32s3_amoled175" build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Manual build for ESP32-C6 AMOLED 1.43

```bash
cd esp-idf
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.esp32c6_amoled143" set-target esp32c6
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.esp32c6_amoled143" build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Configuration

Use `idf.py menuconfig` to configure:

- QNOB hardware target and optional peripherals
- Application defaults such as Wi-Fi credentials and MQTT settings

## Board Capabilities

Touch, battery telemetry, and audio are selected explicitly per board in
`components/board_hal/hal_config.h` rather than being inferred only from pin presence.

| Board | Touch | Battery Telemetry | Audio Output | Audio Input |
|-------|-------|-------------------|--------------|-------------|
| ESP32-S3 Touch LCD 1.28 | CST816S | ADC | No | No |
| ESP32-S3 Touch AMOLED 1.75 | CST9217 | AXP2101 PMU | ES8311 over I2S | ES8311 over I2S |
| ESP32-C6 Touch AMOLED 1.43 | FT6x36-compatible | ADC | ES8311 over I2S | ES8311 over I2S |

## Components

### Board HAL

Provides shared pin definitions and board bring-up using board-specific Kconfig selection layered on top of the ESP-IDF target.
It also declares the board-specific touch, battery, and audio backends used by the rest of the firmware.

### TouchPanel

ESP-IDF I2C touch driver with shared gesture/press handling for:

- CST816S on the ESP32-S3 LCD target
- CST9217 on the ESP32-S3 AMOLED 1.75 target
- FT6x36-compatible controllers on the ESP32-C6 AMOLED target

### KnobController

ESP-IDF UART driver for communication with the external knob controller.

### Storage

NVS-backed configuration storage replacing the Arduino EEPROM flow.

### Display

LovyanGFX-based display stack supporting GC9A01 LCD, the CO5300-compatible QSPI AMOLED path, and the SH8601-compatible QSPI AMOLED path.

### Media

The media stack uses the board capability selection from the HAL:

- ESP32-S3 LCD 1.28 cleanly disables speaker and microphone initialization
- ESP32-S3 AMOLED 1.75 enables ES8311 codec audio over I2S with GPIO amplifier enable
- ESP32-C6 AMOLED 1.43 enables ES8311 codec audio over I2S with EXIO-controlled amplifier enable

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

See the repository root for licensing details.
