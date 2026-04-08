# TouchPanel Component - CST816S Capacitive Touch Controller
idf_component_register(
    SRCS "TouchPanel.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c board_hal
    PRIV_REQUIRES esp_timer
)
