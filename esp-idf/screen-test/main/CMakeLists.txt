idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        board_hal
        driver
        esp_lcd
        esp_lcd_sh8601
)
