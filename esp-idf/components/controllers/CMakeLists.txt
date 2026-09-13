# Controllers Component - Sound and Light controllers
idf_component_register(
    SRCS "SoundController.cpp"
         "screens/NullScreenController.cpp"
         "screens/BootScreenController.cpp"
         "screens/LightScreenController.cpp"
    INCLUDE_DIRS "." "screens"
    REQUIRES display ui board_hal model input
)
