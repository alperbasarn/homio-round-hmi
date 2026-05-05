idf_component_register(
    SRCS "WeatherHandler.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_timer esp_http_client json
)
