# WiFi Manager Component
idf_component_register(
    SRCS "WiFiManager.cpp" "SetupPortal.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif lwip storage esp_http_server connectivity_manager
    PRIV_REQUIRES esp_timer bluetooth_manager
    EMBED_TXTFILES
        "portal/index.html"
        "portal/device-info.html"
)
