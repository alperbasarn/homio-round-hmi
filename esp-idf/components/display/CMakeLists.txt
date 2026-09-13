# Display Component - Display controller and screen management
idf_component_register(
    SRCS "DisplayController.cpp"
         "DisplayBackendFactory.cpp"
         "EspLcdSh8601DisplayFlushBackend.cpp"
         "EspLcdDisplayPanelBackend.cpp"
         "EspLcdGc9a01DisplayFlushBackend.cpp"
         "EspLcdGc9a01DisplayPanelBackend.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver board_hal esp_lcd_sh8601 esp_lcd_co5300 model input
    PRIV_REQUIRES esp_timer controllers ui
)
