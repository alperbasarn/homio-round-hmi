# Host tests for the model layer (BLD-06, BLD-07, BLD-10).
#
# A plain CMake project: no IDF_PATH, no cross toolchain. It compiles the same sources as the
# ESP-IDF `model` component (via components/model/sources.cmake) with the device's language
# flags plus ASan/UBSan and -Werror. Every test_*.cpp in this directory becomes its own
# executable and ctest entry; adding a suite needs no CMake edit.
#
#   cmake -S esp-idf/test/host -B <build> -G Ninja && cmake --build <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.16)
project(qnob_model_host_tests CXX)

if(DEFINED ENV{IDF_PATH} AND NOT "$ENV{IDF_PATH}" STREQUAL "")
    message(WARNING "IDF_PATH is set; the host tests must build with the host compiler, not the cross toolchain")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../components/model/sources.cmake")

# Device flags (BLD-07): the same language level and exception/RTTI settings as the firmware.
set(QNOB_HOST_FLAGS
    -std=gnu++2b -fexceptions -fno-rtti
    -Wall -Wextra -Werror
    -g -fno-omit-frame-pointer
    -fsanitize=address,undefined -fno-sanitize=vptr
    -DQNOB_MODEL_TRACE=1
)

add_library(model STATIC ${MODEL_SOURCES})
target_include_directories(model PUBLIC "${MODEL_INCLUDE_DIR}")
target_compile_options(model PUBLIC ${QNOB_HOST_FLAGS})
target_link_options(model PUBLIC -fsanitize=address,undefined)

add_library(test_support STATIC
    "${CMAKE_CURRENT_LIST_DIR}/support/TestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/support/HeapGuard.cpp"
)
target_include_directories(test_support PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/support"
    "${CMAKE_CURRENT_LIST_DIR}/fakes"
)
target_link_libraries(test_support PUBLIC model)

# APP-21: the command grammar is an ESP-free leaf inside command_handler; compiling it here is
# what proves that.
add_library(command_parsers STATIC "${CMAKE_CURRENT_LIST_DIR}/../../components/command_handler/CommandParsers.cpp")
target_include_directories(command_parsers PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../../components/command_handler")
target_compile_options(command_parsers PUBLIC ${QNOB_HOST_FLAGS})
target_link_options(command_parsers PUBLIC -fsanitize=address,undefined)

# NET-31: the pure poller cores compile on the host from the same files the device uses.
include("${CMAKE_CURRENT_LIST_DIR}/../../components/adapters/sources.cmake")
add_library(adapters_core STATIC ${ADAPTERS_PURE_SOURCES})
target_include_directories(adapters_core PUBLIC "${ADAPTERS_INCLUDE_DIR}")
target_compile_options(adapters_core PUBLIC ${QNOB_HOST_FLAGS})
target_link_options(adapters_core PUBLIC -fsanitize=address,undefined)
target_link_libraries(adapters_core PUBLIC model)

# INPT-06: the input event record compiles on the host from the device file.
add_library(input_core STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../../components/input/src/InputEvent.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/input/src/InputTranslator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/input/src/InputSfxListener.cpp"
)
target_include_directories(input_core PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../../components/input/include")
target_compile_options(input_core PUBLIC ${QNOB_HOST_FLAGS})
target_link_options(input_core PUBLIC -fsanitize=address,undefined)

find_package(Threads REQUIRED)

enable_testing()

# BLD-10: glob registration. CONFIGURE_DEPENDS re-globs on every build, so a new test_*.cpp is
# picked up without touching this file.
file(GLOB QNOB_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/test_*.cpp")
foreach(test_source IN LISTS QNOB_TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)
    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} PRIVATE test_support model command_parsers adapters_core input_core Threads::Threads)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
