#!/usr/bin/env bash
# BLD-08: the model layer must not include platform headers or depend on Kconfig symbols.
# Exit 1 on the first violation, with the offending lines.
set -euo pipefail

MODEL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../components/model" && pwd)"

violations=0
if grep -rnE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](esp_|freertos/|driver/|lvgl|nvs_|hal/|soc/|sdkconfig)' "${MODEL_DIR}"; then
    violations=1
fi
if grep -rnE --include='*.h' --include='*.cpp' '\bCONFIG_[A-Z0-9_]+' "${MODEL_DIR}"; then
    violations=1
fi
if grep -rnE '\b(esp_[a-z_]+\(|lv_[a-z_]+\(|nvs_[a-z_]+\(|xTask[A-Za-z]+\(|vTask[A-Za-z]+\()' "${MODEL_DIR}"; then
    violations=1
fi

if [[ ${violations} -ne 0 ]]; then
    echo "model purity violated: platform headers, Kconfig symbols or platform calls under ${MODEL_DIR}" >&2
    exit 1
fi
echo "model purity ok: ${MODEL_DIR}"
