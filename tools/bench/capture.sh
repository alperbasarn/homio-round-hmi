#!/usr/bin/env bash
# Hold the port and capture the console.   tools/bench/capture.sh SECONDS OUTFILE [--reset] [--send TEXT@SECONDS ...]
# The port stays open across the reset, so nothing from the boot is lost; the PSK is scrubbed.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
bench_require_port
SECS="$1"; OUT="$2"; shift 2
python "$BENCH_ROOT/tools/bench_serial.py" "$BENCH_PORT" "$SECS" "$OUT" "$@"
bench_scrub "$OUT"
