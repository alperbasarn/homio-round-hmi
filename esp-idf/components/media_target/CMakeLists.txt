# media_target (BLD-12, D15): the target contract, the registry and the PC adapter. Pure code;
# public dependency on model only, so nothing above it inherits a transport include path.
include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

idf_component_register(
    SRCS ${MEDIA_TARGET_SOURCES}
    INCLUDE_DIRS "include"
    REQUIRES model
)
