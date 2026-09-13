#!/usr/bin/env bash
# USB-drop diagnostics.   tools/bench/diag.sh [LOG] [SECONDS] [--no-flash]
#   default: flash, reset, hold the port SECONDS (900) with sysInfo at 40 s and 25 s before the end.
#   --no-flash: no flash, NO reset: use this right after pressing RST following a drop, so the
#   boot black box ("previous run ... chip kept power" or "no record ... power was lost") and the
#   AXP2101 power-off reason ("off: ...") of the run that died are read back through sysInfo.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
LOG="${1:-$BENCH_OUT/diag_$(date +%H%M%S).log}"; SECS="${2:-900}"; FLASH=1; [ "${3:-}" = "--no-flash" ] && FLASH=0
if [ "$FLASH" = 1 ]; then
  bash "$BENCH_ROOT/tools/bench/flash.sh" || exit $?
  bash "$BENCH_ROOT/tools/bench/capture.sh" "$SECS" "$LOG" --reset --send "sysInfo@40" --send "sysInfo@$((SECS-25))" || exit $?
else
  bash "$BENCH_ROOT/tools/bench/capture.sh" "$SECS" "$LOG" --send "sysInfo@5" --send "sysInfo@$((SECS-25))" || exit $?
fi
echo "=== health ==="; bench_health "$LOG"
echo "=== boot: reset reason, black box, PMU ==="; grep -n -E 'Reset reason code|Black box|PMU at boot' "$LOG" | cut -c1-230
echo "=== sysInfo ==="; grep -n '^sysInfo:' "$LOG" | cut -c1-230
echo "=== sleep stages ==="; grep -n -E 'SleepHandler: (Stage|Stages|No RTC|Deep sleep refused)' "$LOG" | cut -c1-150
echo "=== heartbeats: $(grep -c 'heartbeat uptime' "$LOG") ==="; grep 'heartbeat uptime' "$LOG" | tail -n 3 | cut -c1-200
echo "=== port after the capture ==="; ls -la /dev/ttyACM* 2>/dev/null || echo "NO $BENCH_PORT after the capture"
echo "log: $LOG"
