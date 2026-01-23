#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN="$ROOT_DIR/so_projekt"
LOG="/tmp/so_projekt.log"
REPORT_BASE="/tmp/so_projekt_report_day_"

log_info() {
  echo "[$(date +%H:%M:%S)] $*" >&2
}

dump_log_tail() {
  echo "--- LOG TAIL (last 50 lines) ---" >&2
  tail -n 50 "$LOG" >&2 || true
  echo "--- END LOG TAIL ---" >&2
}

require_binary() {
  if [[ ! -x "$BIN" ]]; then
    log_info "Brak binarki, budowanie: make -C $ROOT_DIR"
    if ! make -C "$ROOT_DIR" 2>&1 | tee /tmp/so_projekt_build.log; then
      echo "FAIL: build failed (see /tmp/so_projekt_build.log)"
      return 1
    fi
    log_info "Budowanie zakonczone"
  else
    log_info "Binarka istnieje: $BIN"
  fi
}

clean_artifacts() {
  log_info "Czyszczenie logow i raportow"
  : > "$LOG"
  rm -f "${REPORT_BASE}"*.txt
  log_info "Czyszczenie zakonczone"
}

start_director() {
  require_binary
  log_info "Start dyrektora: $BIN $*"
  "$BIN" "$@" >>"$LOG" 2>&1 &
  log_info "Dyrektor PID: $!"
  if kill -0 "$!" 2>/dev/null; then
    log_info "Dyrektor dziala"
  else
    log_info "Dyrektor nie wystartowal"
  fi
  echo $!
}

stop_director() {
  local pid="$1"
  log_info "Zatrzymywanie dyrektora PID: $pid"
  if kill -0 "$pid" 2>/dev/null; then
    kill -INT "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
}

wait_for_log() {
  local pattern="$1"
  local timeout_sec="$2"
  local start
  start=$(date +%s)
  log_info "Czekam na log: $pattern (timeout ${timeout_sec}s)"
  while true; do
    if grep -q "$pattern" "$LOG"; then
      log_info "Znaleziono log: $pattern"
      return 0
    fi
    if (( $(date +%s) - start >= timeout_sec )); then
      log_info "Timeout: $pattern"
      dump_log_tail
      return 1
    fi
    sleep 0.1
  done
}

assert_log() {
  local pattern="$1"
  log_info "Sprawdzam log: $pattern"
  if ! grep -q "$pattern" "$LOG"; then
    echo "FAIL: missing log pattern: $pattern"
    dump_log_tail
    return 1
  fi
}

assert_report_contains() {
  local day="$1"
  local pattern="$2"
  local report="${REPORT_BASE}${day}.txt"
  log_info "Sprawdzam raport: $report (pattern: $pattern)"
  if [[ ! -f "$report" ]]; then
    echo "FAIL: missing report file: $report"
    return 1
  fi
  if ! grep -q "$pattern" "$report"; then
    echo "FAIL: missing report pattern: $pattern"
    return 1
  fi
}
