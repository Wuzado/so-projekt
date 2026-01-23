#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/lib.sh"

log_info "TEST 3: SIGUSR1 urzednik"
clean_artifacts

pid=$(start_director --role dyrektor --Tp 8 --Tk 9 --time-mul 2000 --gen-from-dyrektor --gen-min-delay 0 --gen-max-delay 1)
trap 'stop_director "$pid"' EXIT

sleep 1

urzednik_pid=$(pgrep -f "so_projekt --role urzednik" | head -n 1 || true)
if [[ -z "$urzednik_pid" ]]; then
  echo "FAIL: no urzednik process found"
  exit 1
fi

log_info "Wysylam SIGUSR1 do urzednika PID: $urzednik_pid"
kill -USR1 "$urzednik_pid"

if ! wait_for_log "Urzednik zakonczyl prace." 5; then
  echo "FAIL: timeout waiting for urzednik shutdown"
  exit 1
fi

if ! wait_for_log "skierowanie do" 5; then
  echo "FAIL: timeout waiting for report entries"
  exit 1
fi

assert_report_contains 1 "skierowanie do"

stop_director "$pid"
trap - EXIT

echo "PASS: Test 3"
