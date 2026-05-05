# Command Handler Component - Serial/UART command processing
idf_component_register(
    SRCS "CommandHandler.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver storage knob_controller wifi_manager mqtt_manager board_hal controllers ota_manager bluetooth_manager
    PRIV_REQUIRES display
)
