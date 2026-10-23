idf_component_register(
    SRCS "BootGuard.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash freertos
)
