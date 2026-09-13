#!/usr/bin/env bash
# Flash, then capture 90 s (help at 45 s, info at 60 s) and summarise the boot.   tools/bench/boot_check.sh [LOG]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
LOG="${1:-$BENCH_OUT/boot_$(date +%H%M%S).log}"
bash "$BENCH_ROOT/tools/bench/flash.sh" || exit $?
bash "$BENCH_ROOT/tools/bench/capture.sh" 90 "$LOG" --reset --send "help@45" --send "sysInfo@60" | head -n 1
echo "=== health ==="; bench_health "$LOG"
echo "=== boot timeline ==="
grep -n -E 'Reset reason code|Black box|PMU at boot|Boot screen up|Got IP address|Boot handoff|INITIALIZATION -> INFO|MQTT connected|MQTT broker URL not configured|LED bridge up|heartbeat uptime|^sysInfo:' "$LOG" | cut -c1-170 | head -n 40
echo "=== model drains ==="
echo "drain lines: $(grep -c 'MODEL: drain' "$LOG")"; grep -o 'offtask=[0-9]*' "$LOG" | sort | uniq -c
echo "log: $LOG"
