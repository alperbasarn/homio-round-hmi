# UI Component - Graphics primitives, UI elements, and screens
idf_component_register(
    SRCS "Arc.cpp"
         "Buttons.cpp"
         "LvglDisplay.cpp"
         "fonts/lv_font_montserrat_72_time.c"
         "fonts/lv_font_montserrat_96_time.c"
         "fonts/lv_font_montserrat_64_info.c"
         "screens/InitializationScreen.cpp"
         "screens/EnvironmentInfoScreen.cpp"
         "screens/DeviceInfoScreen.cpp"
            "screens/PlaceholderPageScreen.cpp"
         "screens/ModeController.cpp"
    INCLUDE_DIRS "." "screens"
    REQUIRES display touch_panel board_hal lvgl LovyanGFX
    PRIV_REQUIRES esp_timer
)
