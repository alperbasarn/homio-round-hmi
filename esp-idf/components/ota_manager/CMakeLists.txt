idf_component_register(
    SRCS "OTAManager.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_https_ota app_update esp_http_client wifi_manager mbedtls esp_timer json freertos
)
