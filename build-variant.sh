#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}/esp-idf"
OUTPUT_ROOT="${SCRIPT_DIR}/Hardware Ref"
PUBLISH_ROOT="${SCRIPT_DIR}/Binaries"
OTA_ROOT="${SCRIPT_DIR}/OTA"
# dependencies.lock is per IDF target (dependencies.lock.<target>) and committed; no save/restore dance (BLD-04).

if [[ $# -lt 1 ]]; then
    echo "Usage: bash build-variant.sh <esp32s3_lcd128|esp32s3_amoled175|esp32c6_amoled143>" >&2
    exit 1
fi

VARIANT="$1"

case "${VARIANT}" in
    esp32s3_lcd128)
        DISPLAY_NAME="ESP32-S3 LCD 1.28"
        IDF_TARGET_NAME="esp32s3"
        OTA_NAME_PREFIX="S3-128"
        SDKCONFIG_DEFAULTS_ARG="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.esp32s3_lcd128"
        BUILD_DIR="${OUTPUT_ROOT}/build_s3_lcd128"
        SDKCONFIG_OUT="${OUTPUT_ROOT}/sdkconfig.esp32s3_lcd128"
        ;;
    esp32s3_amoled175)
        DISPLAY_NAME="ESP32-S3 AMOLED 1.75"
        IDF_TARGET_NAME="esp32s3"
        OTA_NAME_PREFIX="S3-175"
        SDKCONFIG_DEFAULTS_ARG="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.esp32s3_amoled175"
        BUILD_DIR="${OUTPUT_ROOT}/build_s3_amoled175"
        SDKCONFIG_OUT="${OUTPUT_ROOT}/sdkconfig.esp32s3_amoled175"
        ;;
    esp32c6_amoled143)
        DISPLAY_NAME="ESP32-C6 AMOLED 1.43"
        IDF_TARGET_NAME="esp32c6"
        OTA_NAME_PREFIX="C6-143"
        SDKCONFIG_DEFAULTS_ARG="sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.esp32c6_amoled143"
        BUILD_DIR="${OUTPUT_ROOT}/build_c6_amoled143"
        SDKCONFIG_OUT="${OUTPUT_ROOT}/sdkconfig.esp32c6_amoled143"
        ;;
    *)
        echo "Unknown variant: ${VARIANT}" >&2
        exit 1
        ;;
esac

# Bench-local overlay (untracked): appended last so it overrides the tracked defaults (BLD-20).
if [[ -f "${PROJECT_DIR}/sdkconfig.defaults.local" ]]; then
    SDKCONFIG_DEFAULTS_ARG+=";sdkconfig.defaults.local"
fi
# The dependencies lock is selected per target by esp-idf/CMakeLists.txt (dependencies.lock.<target>, BLD-04).

echo "[${VARIANT}] Building ${DISPLAY_NAME}..."
echo "[${VARIANT}] Output dir: ${BUILD_DIR}"
echo "[${VARIANT}] sdkconfig defaults: ${SDKCONFIG_DEFAULTS_ARG}"
echo "[${VARIANT}] dependencies lock: dependencies.lock.${IDF_TARGET_NAME} (set by esp-idf/CMakeLists.txt)"

source ~/esp/esp-idf/export.sh >/dev/null
cd "${PROJECT_DIR}"

idf.py \
    -B "${BUILD_DIR}" \
    -DIDF_TARGET="${IDF_TARGET_NAME}" \
    -DSDKCONFIG="${SDKCONFIG_OUT}" \
    -DSDKCONFIG_DEFAULTS="${SDKCONFIG_DEFAULTS_ARG}" \
    reconfigure build

APP_NAME="qnob-screen"
ARTIFACT_DIR="${PUBLISH_ROOT}/${VARIANT}"
mkdir -p "${ARTIFACT_DIR}"
mkdir -p "${OTA_ROOT}"

REQUIRED_FILES=(
    "${BUILD_DIR}/${APP_NAME}.bin"
    "${BUILD_DIR}/${APP_NAME}.elf"
    "${BUILD_DIR}/${APP_NAME}.map"
    "${BUILD_DIR}/bootloader/bootloader.bin"
    "${BUILD_DIR}/partition_table/partition-table.bin"
    "${BUILD_DIR}/ota_data_initial.bin"
    "${BUILD_DIR}/flash_args"
    "${BUILD_DIR}/flasher_args.json"
    "${BUILD_DIR}/project_description.json"
)

for path in "${REQUIRED_FILES[@]}"; do
    if [[ ! -f "${path}" ]]; then
        echo "[${VARIANT}] Expected artifact not found: ${path}" >&2
        exit 1
    fi
done

cp -f "${BUILD_DIR}/${APP_NAME}.bin" "${ARTIFACT_DIR}/${APP_NAME}.bin"
cp -f "${BUILD_DIR}/${APP_NAME}.elf" "${ARTIFACT_DIR}/${APP_NAME}.elf"
cp -f "${BUILD_DIR}/${APP_NAME}.map" "${ARTIFACT_DIR}/${APP_NAME}.map"
cp -f "${BUILD_DIR}/bootloader/bootloader.bin" "${ARTIFACT_DIR}/bootloader.bin"
cp -f "${BUILD_DIR}/partition_table/partition-table.bin" "${ARTIFACT_DIR}/partition-table.bin"
cp -f "${BUILD_DIR}/ota_data_initial.bin" "${ARTIFACT_DIR}/ota_data_initial.bin"
cp -f "${BUILD_DIR}/flash_args" "${ARTIFACT_DIR}/flash_args"
cp -f "${BUILD_DIR}/flasher_args.json" "${ARTIFACT_DIR}/flasher_args.json"

# Report-only first-party component graph (BLD-45); never fails the build.
python "${SCRIPT_DIR}/tools/check-component-graph.py" "${BUILD_DIR}/project_description.json" || true

PROJECT_VERSION="$(sed -n 's/.*"project_version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "${BUILD_DIR}/project_description.json" | head -n 1)"
if [[ -z "${PROJECT_VERSION}" ]]; then
    PROJECT_VERSION="unknown"
fi

SAFE_VERSION="$(printf '%s' "${PROJECT_VERSION}" | tr -c 'A-Za-z0-9._-' '_')"
OTA_ARTIFACT_PATH="${OTA_ROOT}/${OTA_NAME_PREFIX}-V${SAFE_VERSION}.bin"
cp -f "${BUILD_DIR}/${APP_NAME}.bin" "${OTA_ARTIFACT_PATH}"

echo "[${VARIANT}] Published artifacts: ${ARTIFACT_DIR}"
echo "[${VARIANT}] Published OTA artifact: ${OTA_ARTIFACT_PATH}"
