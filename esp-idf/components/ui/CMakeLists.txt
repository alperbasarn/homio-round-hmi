# UI Component - Graphics primitives, UI elements, and screens
idf_component_register(
    SRCS "Arc.cpp"
         "Buttons.cpp"
         "LvglDisplay.cpp"
         "screens/InitializationScreen.cpp"
         "screens/EnvironmentInfoScreen.cpp"
         "screens/DeviceInfoScreen.cpp"
            "screens/PlaceholderPageScreen.cpp"
         "screens/ModeController.cpp"
    INCLUDE_DIRS "." "screens"
    REQUIRES display touch_panel board_hal lvgl LovyanGFX
    PRIV_REQUIRES esp_timer
)
