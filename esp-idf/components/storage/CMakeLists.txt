# Storage Component - NVS-based configuration storage
idf_component_register(
    SRCS "NVSManager.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash
)
