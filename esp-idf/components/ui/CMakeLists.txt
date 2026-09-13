# UI Component - Graphics primitives, UI elements, and screens
idf_component_register(
    SRCS "LvglDisplay.cpp"
         "theme/Theme.cpp"
         "views/InitializationView.cpp"
         "views/LightView.cpp"
         "views/MediaView.cpp"
         "fonts/lv_font_montserrat_72_time.c"
         "fonts/lv_font_montserrat_96_time.c"
         "fonts/lv_font_montserrat_64_info.c"
         "screens/EnvironmentInfoScreen.cpp"
         "screens/DeviceInfoScreen.cpp"
         "screens/PlaceholderPageScreen.cpp"
         "screens/ModeController.cpp"
    INCLUDE_DIRS "." "screens" "theme" "views"
    REQUIRES display board_hal lvgl model input
    PRIV_REQUIRES esp_timer
)
