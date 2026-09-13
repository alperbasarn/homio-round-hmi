# Controllers Component - Sound and Light controllers
idf_component_register(
    SRCS "SoundController.cpp" "LightController.cpp"
         "screens/NullScreenController.cpp"
    INCLUDE_DIRS "." "screens"
    REQUIRES display ui board_hal model input
)
