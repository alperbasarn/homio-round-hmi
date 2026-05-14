idf_component_register(
    SRCS "MdnsAdvertiser.cpp"
    INCLUDE_DIRS "."
    REQUIRES espressif__mdns esp_wifi esp_app_format storage
)
