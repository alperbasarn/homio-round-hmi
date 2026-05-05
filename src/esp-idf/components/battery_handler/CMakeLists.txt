idf_component_register(
    SRCS "BatteryHandler.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_timer esp_adc board_hal
)
