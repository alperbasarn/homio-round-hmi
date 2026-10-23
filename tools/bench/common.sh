#!/usr/bin/env bash
# Shared bits of the bench scripts (WSL, user buildserver, IDF 5.3 at ~/esp/esp-idf).
# Logs go to helpers/bench_logs (git-ignored); the bench PSK never reaches a log.
BENCH_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BENCH_PORT="${BENCH_PORT:-/dev/ttyACM0}"
BENCH_OUT="${BENCH_OUT:-$BENCH_ROOT/helpers/bench_logs}"
BENCH_BUILD="$BENCH_ROOT/helpers/build_s3_amoled175"
mkdir -p "$BENCH_OUT"
# shellcheck disable=SC1090
source "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1
bench_require_port() {
  [ -e "$BENCH_PORT" ] || { echo "no serial device at $BENCH_PORT (usbipd attach?)"; ls -la /dev/ttyACM* 2>/dev/null; exit 2; }
}
bench_scrub() {
  export PSK="$(grep '^CONFIG_QNOB_WIFI_PASSWORD=' "$BENCH_ROOT/src/esp-idf/sdkconfig.defaults.local" 2>/dev/null | cut -d= -f2- | tr -d '"')"
  if [ -n "$PSK" ] && [ -f "$1" ]; then perl -pi -e 's/\Q$ENV{PSK}\E/<psk>/g' "$1"; fi
}
bench_health() {
  local log="$1"
  echo "lines: $(wc -l < "$log")  watchdog: $(grep -c 'task_wdt: Task watchdog' "$log")  panic: $(grep -c -E 'Guru Meditation|abort\(\) was called|^Backtrace:' "$log")  lvgl: $(grep -c -E '^\[(Error|Warn)\]|Asserted at' "$log")"
}
