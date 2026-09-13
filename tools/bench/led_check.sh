#!/usr/bin/env bash
# LED path over the console: hold the port SECONDS (default 120), list the strips at 5 s and 20 s,
# run the extra console lines given as TEXT@SECONDS, show the bridge counters at the end.
#   tools/bench/led_check.sh [LOG] [SECONDS] [--send TEXT@SECONDS ...]     e.g. --send "ledPair:0@30"
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
LOG="${1:-$BENCH_OUT/led_$(date +%H%M%S).log}"; SECS="${2:-120}"; shift 2 2>/dev/null
bash "$BENCH_ROOT/tools/bench/capture.sh" "$SECS" "$LOG" --send "leds@5" --send "ledStatus@6" --send "leds@20" "$@" --send "leds@$((SECS-10))" --send "ledStatus@$((SECS-8))" || exit $?
echo "=== health ==="; bench_health "$LOG"
echo "=== LED bridge ==="; grep -n -E 'LedBridge|^leds:|^  [0-7] led-|^ledStatus:|^ledPair|^ledCode|^ledSelect|^ledUnpair|MQTT connected|dispatch topic=homio' "$LOG" | cut -c1-200 | head -n 60
echo "=== light model trace (last 5) ==="; grep 'MODEL: light' "$LOG" | tail -n 5 | cut -c1-160
echo "log: $LOG"
