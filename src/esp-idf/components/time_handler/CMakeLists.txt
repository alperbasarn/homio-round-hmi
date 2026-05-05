idf_component_register(
    SRCS "TimeHandler.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_timer freertos esp_netif lwip
)
