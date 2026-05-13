idf_component_register(
    SRCS "CommandServer.cpp"
    INCLUDE_DIRS "."
    REQUIRES command_handler command_envelope esp_timer freertos lwip
)
