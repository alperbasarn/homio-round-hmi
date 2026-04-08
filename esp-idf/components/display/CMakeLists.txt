# Display Component - Display controller and screen management
idf_component_register(
    SRCS "DisplayController.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver board_hal touch_panel LovyanGFX knob_controller
    PRIV_REQUIRES esp_timer controllers ui sleep_handler
)
