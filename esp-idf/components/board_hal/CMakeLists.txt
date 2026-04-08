# HAL Component - Hardware Abstraction Layer
idf_component_register(
    SRCS "hal_init.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c
)
