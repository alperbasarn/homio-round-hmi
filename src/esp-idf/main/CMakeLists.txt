# Main Application
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        board_hal
        display
        touch_panel
        knob_controller
        storage
        LovyanGFX
        driver
        nvs_flash
        command_handler
        wifi_manager
        mqtt_manager
        internet_handler
        controllers
        ui
        media
        battery_handler
        ota_manager
        sleep_handler
        bluetooth_manager
        connectivity_manager
)
