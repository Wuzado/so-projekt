#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/lib.sh"

log_info "TEST 2: Limity przyjec"
clean_artifacts

pid=$(start_director --role dyrektor --Tp 8 --Tk 9 --time-mul 2000 --X1 1 --X2 1 --X3 1 --X4 1 --X5 1 --gen-from-dyrektor --gen-min-delay 0 --gen-max-delay 0)
trap 'stop_director "$pid"' EXIT

if ! wait_for_log "Brak wolnych terminow" 5; then
  echo "FAIL: timeout waiting for limit rejection"
  exit 1
fi

assert_log "Brak wolnych terminow w wydziale"
assert_log "Brak wolnych terminow - bilet nie zostal wydany."

stop_director "$pid"
trap - EXIT

echo "PASS: Test 2"
