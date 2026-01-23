#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/lib.sh"

log_info "TEST 4: SIGUSR2 ewakuacja"
clean_artifacts

pid=$(start_director --role dyrektor --Tp 8 --Tk 9 --time-mul 2000 --gen-from-dyrektor --gen-min-delay 0 --gen-max-delay 1)
trap 'stop_director "$pid"' EXIT

sleep 1

log_info "Wysylam SIGUSR2 do petentow"
pkill -USR2 -f "so_projekt --role petent" || true

if ! wait_for_log "Ewakuacja - petent opuszcza budynek." 5; then
  echo "FAIL: timeout waiting for evacuation logs"
  exit 1
fi

stop_director "$pid"
trap - EXIT

echo "PASS: Test 4"
