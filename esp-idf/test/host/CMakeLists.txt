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

find_package(Threads REQUIRED)

enable_testing()

# BLD-10: glob registration. CONFIGURE_DEPENDS re-globs on every build, so a new test_*.cpp is
# picked up without touching this file.
file(GLOB QNOB_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/test_*.cpp")
foreach(test_source IN LISTS QNOB_TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)
    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} PRIVATE test_support model Threads::Threads)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
