# Shared between the ESP-IDF component and test/host: the pure poller cores (NET-06, NET-07,
# PERI-06). They include only model headers; the driver-facing wrappers in the same files are
# fenced by ESP_PLATFORM and never see the host compiler.
set(ADAPTERS_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(ADAPTERS_INCLUDE_DIR "${ADAPTERS_DIR}/include")
set(ADAPTERS_PURE_SOURCES
    "${ADAPTERS_DIR}/src/ConnectivityPoller.cpp"
    "${ADAPTERS_DIR}/src/EnvironmentPoller.cpp"
    "${ADAPTERS_DIR}/src/BatteryPoller.cpp"
)
