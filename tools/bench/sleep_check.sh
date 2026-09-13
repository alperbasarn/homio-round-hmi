#!/usr/bin/env bash
# Sleep-stage regression: flash, then hold the port 260 s (sysInfo at 40 s and 235 s).   tools/bench/sleep_check.sh [LOG]
# Expected: stage 0 at 20 s, stage 1 at 30 s, "Stages 2 and 3 held" and "Stage 2 skipped" at 120 s,
# "Stage 3 skipped" at 180 s, a heartbeat every 30 s, the port still present at the end.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
LOG="${1:-$BENCH_OUT/sleep_$(date +%H%M%S).log}"
grep -n -E 'QNOB_(LIGHT|DEEP)_SLEEP' "$BENCH_ROOT/Hardware Ref/sdkconfig.esp32s3_amoled175"
bash "$BENCH_ROOT/tools/bench/flash.sh" || exit $?
bash "$BENCH_ROOT/tools/bench/capture.sh" 260 "$LOG" --reset --send "sysInfo@40" --send "sysInfo@235" | head -n 1
echo "=== health ==="; bench_health "$LOG"
echo "=== sleep stages ==="; grep -n -E 'SleepHandler: (Stage|Stages|Reducing|Restoring|No RTC|Deep sleep refused)' "$LOG" | cut -c1-150
echo "=== heartbeats: $(grep -c 'heartbeat uptime' "$LOG") ==="; grep 'heartbeat uptime' "$LOG" | tail -n 2 | cut -c1-170
echo "=== port after the capture ==="; ls -la /dev/ttyACM* 2>/dev/null || echo "NO $BENCH_PORT after the capture"
echo "log: $LOG"
