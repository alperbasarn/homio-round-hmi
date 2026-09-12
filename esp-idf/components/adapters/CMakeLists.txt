# adapters: transport bridges and pollers that push into the model (D15, BLD-24). One component
# for the MQTT bridge and the three pollers. The public dependency is the model only; every
# driver it talks to is private, so nothing above it inherits a driver include path.
idf_component_register(
    SRCS "src/Adapters.cpp"
    INCLUDE_DIRS "include"
    REQUIRES model
    PRIV_REQUIRES mqtt_manager wifi_manager internet_handler battery_handler esp_timer
)
