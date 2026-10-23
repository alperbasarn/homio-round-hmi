# adapters: transport bridges and pollers that push into the model (D15, BLD-24). One component
# for the MQTT bridge and the three pollers. The public dependency is the model only; every
# driver it talks to is private, so nothing above it inherits a driver include path.
#
# The poller cores are pure and are also compiled by test/host through sources.cmake; the
# driver-facing wrappers and ModelHost exist only under ESP_PLATFORM.
include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

idf_component_register(
    SRCS ${ADAPTERS_PURE_SOURCES} "src/Adapters.cpp" "src/ModelHost.cpp" "src/MqttModelBridge.cpp"
    INCLUDE_DIRS "include"
    REQUIRES model
    PRIV_REQUIRES mqtt_manager wifi_manager internet_handler time_handler weather_handler battery_handler esp_timer
)
