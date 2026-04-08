#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_VARIANT_SCRIPT="${SCRIPT_DIR}/build-variant.sh"

if [[ $# -gt 0 ]]; then
    VARIANTS=("$@")
else
    VARIANTS=(
        "esp32s3_lcd128"
        "esp32s3_amoled175"
        "esp32c6_amoled143"
    )
fi

FAILED=0

for variant in "${VARIANTS[@]}"; do
    echo
    echo "=== ${variant} ==="
    if ! bash "${BUILD_VARIANT_SCRIPT}" "${variant}"; then
        echo "[${variant}] FAILED" >&2
        FAILED=1
    fi
done

if [[ ${FAILED} -ne 0 ]]; then
    exit 1
fi
