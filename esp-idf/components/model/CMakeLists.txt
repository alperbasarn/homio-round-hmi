# model: the MVC model layer. Pure C++ with no platform includes, so it builds as an ESP-IDF
# component here and as a plain static library under test/host (BLD-05). It is the leaf of the
# layering: no project-local REQUIRES, ever.
include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${MODEL_SOURCES}
        INCLUDE_DIRS "include"
    )
else()
    add_library(model STATIC ${MODEL_SOURCES})
    target_include_directories(model PUBLIC "${MODEL_INCLUDE_DIR}")
endif()
