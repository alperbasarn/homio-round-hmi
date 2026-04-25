# WiFi Manager Component
idf_component_register(
    SRCS "WiFiManager.cpp" "SetupPortal.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif lwip storage esp_http_server
    PRIV_REQUIRES esp_timer
)
