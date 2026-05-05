# QNOB Control System

A comprehensive control system for managing the QNOB (Quantum Knob) device, allowing you to control media playback, volume, and device settings from your PC.

## Features

- **Sound & Media Control**: Manage volume and media playback directly from your PC
- **Device Connection**: Connect to QNOB via Serial or TCP
- **Configuration Management**: View and edit device settings
- **MQTT Integration**: Synchronize with MQTT for remote control

## Components

### PC Application

The PC application allows you to connect to and control the QNOB device. It features:

- Sound and media control interface
- Serial and TCP connection options
- Configuration editor for device settings
- Real-time communication with the device

### ESP32 Firmware

The ESP32 firmware provides:

- TCP and Serial command interface
- Display control for the onboard screen
- MQTT connectivity for remote control
- EEPROM management for persistent settings

## Project Structure

```
QNOB/
├── main.py                      # Application entry point
├── sound_controller_app.py      # Main application class
├── connect_tab.py               # Connect tab implementation
├── configure_tab.py             # Configure tab implementation
├── sound_controller_tab.py      # Sound controller tab implementation
├── mqtt_handler.py              # MQTT communication handler
├── media_controller.py          # Media playback controller
├── windows_audio_controller.py  # Windows volume controller
└── requirements.txt             # Python dependencies
```

## Installation

### Requirements

- Python 3.8 or higher
- ESP32 with Arduino IDE support

### PC Application Setup

1. Clone the repository
2. Install dependencies:
   ```
   pip install -r requirements.txt
   ```
3. Run the application:
   ```
   python main.py
   ```

### ESP32 Firmware Setup

1. Open the ESP32 code in Arduino IDE
2. Install required libraries:
   - Arduino_GFX_Library
   - PubSubClient
   - Arduino JSON
3. Upload the firmware to your ESP32 device

## Usage

1. Launch the application
2. Connect to your QNOB device via Serial or TCP
3. Use the Sound & Media tab to control playback and volume
4. Use the Configure tab to adjust device settings

## Commands

The QNOB device supports various commands for configuration and control:

- **Volume Control**: `setpoint=<0-100>`
- **Media Control**: `play`, `pause`, `forward`, `rewind`
- **MQTT Configuration**: `configureMQTTServer:<Host>:<Port>:<Username>:<Password>`
- **WiFi Configuration**: `connectWifi:<SSID>:<Password>[:<Slot>]`
- **Device Configuration**: `setDeviceName:<name>`
- **List NVS Values**: `listNVSValues`

Run the `help` command for a complete list of available commands.

## Troubleshooting

- **Connection Issues**: Ensure the device is powered on and the correct COM port is selected
- **MQTT Problems**: Verify MQTT server settings and credentials
- **Media Control Not Working**: Check that media applications are running on your PC

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.
