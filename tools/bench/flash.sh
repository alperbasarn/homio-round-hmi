#!/usr/bin/env bash
# Flash the 1.75 build over the native USB-Serial/JTAG port.   tools/bench/flash.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
bench_require_port
cd "$BENCH_ROOT/src/esp-idf" || exit 1
echo "=== flashing $BENCH_BUILD to $BENCH_PORT ==="
idf.py -B "$BENCH_BUILD" -p "$BENCH_PORT" flash 2>&1 | grep -E 'Hash of data verified|Hard resetting|error|Error|failed' | tail -n 6
echo "flash exit: ${PIPESTATUS[0]}"
