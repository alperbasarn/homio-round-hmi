idf_component_register(
    SRCS "InternetHandler.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_timer lwip wifi_manager time_handler weather_handler storage
)
