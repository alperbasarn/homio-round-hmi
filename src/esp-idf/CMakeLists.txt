# This is the root CMakeLists.txt file.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# One committed dependencies lock per IDF target (BLD-04). The component manager reads the
# lock path from this build property (tools/cmake/build.cmake, prepare_dependencies); a
# -DDEPENDENCIES_LOCK cache variable is not propagated to it, so it must be set here.
idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${IDF_TARGET}")
project(qnob-screen)
