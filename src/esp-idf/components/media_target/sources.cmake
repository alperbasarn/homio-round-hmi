# Shared between the ESP-IDF component and test/host: everything in media_target is pure (model
# headers only). The device composition supplies the publish function; nothing here includes ESP.
set(MEDIA_TARGET_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(MEDIA_TARGET_INCLUDE_DIR "${MEDIA_TARGET_DIR}/include")
set(MEDIA_TARGET_SOURCES
    "${MEDIA_TARGET_DIR}/src/MediaTargetRegistry.cpp"
    "${MEDIA_TARGET_DIR}/src/PcProtocol.cpp"
    "${MEDIA_TARGET_DIR}/src/PcMediaAdapter.cpp"
)
