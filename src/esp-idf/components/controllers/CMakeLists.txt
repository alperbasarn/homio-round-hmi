# Controllers Component - the screen controllers and the knob binding machine
idf_component_register(
    SRCS "KnobBindingMachine.cpp"
         "screens/NullScreenController.cpp"
         "screens/BootScreenController.cpp"
         "screens/LightScreenController.cpp"
         "screens/MediaScreenController.cpp"
         "screens/DeviceInfoScreenController.cpp"
    INCLUDE_DIRS "." "screens"
    REQUIRES display ui board_hal model input media_target
)
