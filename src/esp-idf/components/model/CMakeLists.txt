# model: the MVC model layer. Pure C++ with no platform includes, so it builds as an ESP-IDF
# component here and as a plain static library under test/host (BLD-05). It is the leaf of the
# layering: no project-local REQUIRES, ever.
include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${MODEL_SOURCES}
        INCLUDE_DIRS "include"
    )
    if(CONFIG_QNOB_MODEL_TRACE)
        # MDL-32: the trace is compiled in only when the Kconfig switch is on (off by default).
        target_compile_definitions(${COMPONENT_LIB} PUBLIC QNOB_MODEL_TRACE=1)
    endif()
else()
    add_library(model STATIC ${MODEL_SOURCES})
    target_include_directories(model PUBLIC "${MODEL_INCLUDE_DIR}")
endif()
