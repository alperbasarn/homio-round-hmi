idf_component_register(
    SRCS "MdnsAdvertiser.cpp"
    INCLUDE_DIRS "."
    REQUIRES mdns esp_wifi esp_app_format storage
)
