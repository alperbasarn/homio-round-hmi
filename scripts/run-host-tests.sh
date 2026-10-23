#!/usr/bin/env bash
# BLD-09: configure, build and run the model layer's host tests with the host compiler.
# Deliberately does not source the ESP-IDF export script: the tests must build with g++, never
# with the cross toolchain.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TEST_DIR="${REPO_ROOT}/src/esp-idf/test/host"
BUILD_DIR="${REPO_ROOT}/helpers/build_host_tests"

bash "${TEST_DIR}/check_model_purity.sh"

cmake -S "${TEST_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=g++ >/dev/null
cmake --build "${BUILD_DIR}"
echo "compiler: $(grep -m1 'CMAKE_CXX_COMPILER:' "${BUILD_DIR}/CMakeCache.txt" | cut -d= -f2)"
ctest --test-dir "${BUILD_DIR}" --output-on-failure
