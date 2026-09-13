# Command Handler Component - Serial/UART command processing
idf_component_register(
    SRCS "CommandHandler.cpp" "CommandParsers.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver storage knob_controller wifi_manager mqtt_manager board_hal controllers ota_manager model local_audio
    PRIV_REQUIRES display
)
