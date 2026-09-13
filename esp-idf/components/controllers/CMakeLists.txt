# Controllers Component - Sound and Light controllers
idf_component_register(
    SRCS "SoundController.cpp" "LightController.cpp"
    INCLUDE_DIRS "."
    REQUIRES display ui board_hal model input
)
