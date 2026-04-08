# WiFi Manager Component
idf_component_register(
    SRCS "WiFiManager.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif lwip storage
    PRIV_REQUIRES esp_timer
)
