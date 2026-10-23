# Main Application
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        boot_guard
        board_hal
        display
        touch_panel
        knob_controller
        storage
        driver
        nvs_flash
        command_handler
        mdns_advertiser
        wifi_manager
        mqtt_manager
        internet_handler
        controllers
        ui
        media
        battery_handler
        ota_manager
        sleep_handler
        model
        adapters
        media_target
        input
        local_audio
        esp_driver_usb_serial_jtag
        bluetooth_manager
        connectivity_manager
        esp_wifi
)
