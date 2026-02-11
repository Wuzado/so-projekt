#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/lib.sh"

log_info "TEST 1: Standardowy przebieg dnia"
clean_artifacts

log_info "Wywolanie start_director"
pid=$(start_director --role dyrektor --Tp 8 --Tk 9 --time-mul 2000 --gen-from-dyrektor --gen-min-delay 1 --gen-max-delay 2)
trap 'stop_director "$pid"' EXIT
log_info "PID dyrektora (test 1): $pid"

if ! wait_for_log "Koniec dnia." 20; then
  echo "FAIL: timeout waiting for end of day"
  exit 1
fi

assert_log "Dzien 1: Urzad otwarty."
assert_log "Urzad zamkniety."
assert_log "Koniec dnia."
assert_log "Wydano bilet nr"

stop_director "$pid"
trap - EXIT

echo "PASS: Test 1"
