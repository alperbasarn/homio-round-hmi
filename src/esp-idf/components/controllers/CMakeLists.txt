# Controllers Component - Sound and Light controllers
idf_component_register(
    SRCS "SoundController.cpp" "LightController.cpp" "MediaController.cpp"
    INCLUDE_DIRS "."
    REQUIRES display touch_panel ui board_hal media
)
