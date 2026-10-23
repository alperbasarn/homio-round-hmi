# Shared between the ESP-IDF component (CMakeLists.txt, when ESP_PLATFORM is set) and the host
# test project (test/host/CMakeLists.txt). The model layer must stay free of platform headers;
# test/host/check_model_purity.sh enforces it (BLD-05, BLD-08).
set(MODEL_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(MODEL_INCLUDE_DIR "${MODEL_DIR}/include")
set(MODEL_SOURCES
    "${MODEL_DIR}/src/MutationQueue.cpp"
    "${MODEL_DIR}/src/AppModel.cpp"
    "${MODEL_DIR}/src/Bands.cpp"
    "${MODEL_DIR}/src/ColorMath.cpp"
    "${MODEL_DIR}/src/DateFormat.cpp"
    "${MODEL_DIR}/src/RateLimit.cpp"
    "${MODEL_DIR}/src/PowerRules.cpp"
    "${MODEL_DIR}/src/MediaModel.cpp"
    "${MODEL_DIR}/src/LightModel.cpp"
    "${MODEL_DIR}/src/LedRegistryModel.cpp"
    "${MODEL_DIR}/src/ClimateModel.cpp"
    "${MODEL_DIR}/src/EnvironmentModel.cpp"
    "${MODEL_DIR}/src/ConnectivityModel.cpp"
    "${MODEL_DIR}/src/PowerModel.cpp"
    "${MODEL_DIR}/src/NavigationModel.cpp"
    "${MODEL_DIR}/src/KnobModel.cpp"
    "${MODEL_DIR}/src/TraceHooks.cpp"
    "${MODEL_DIR}/src/ModelTrace.cpp"
)
