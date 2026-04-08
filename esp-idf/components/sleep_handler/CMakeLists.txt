# Sleep Handler Component - Power management and deep sleep
idf_component_register(
    SRCS "SleepHandler.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver board_hal esp_hw_support
    PRIV_REQUIRES esp_timer
)
