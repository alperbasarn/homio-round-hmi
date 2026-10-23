# Command Handler Component - Serial/UART command processing
idf_component_register(
    SRCS "CommandHandler.cpp" "CommandParsers.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver storage wifi_manager mqtt_manager board_hal controllers ota_manager model local_audio bluetooth_manager freertos json command_envelope esp_timer esp_wifi nvs_flash
    PRIV_REQUIRES display esp_driver_usb_serial_jtag adapters
)
