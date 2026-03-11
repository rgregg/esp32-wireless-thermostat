#!/usr/bin/env bash
# OTA soak test — repeatedly flash devices and verify they come back online.
# Usage: ./scripts/ota_soak_test.sh [iterations] [target]
#   iterations: number of OTA rounds (default: 5)
#   target: "controller", "display", or "all" (default: all)
#
# Exit code: 0 if all iterations succeeded, 1 if any failure.

set -euo pipefail

ITERATIONS=${1:-5}
TARGET=${2:-all}

CONTROLLER_IP="${CONTROLLER_IP:-192.168.42.94}"
DISPLAY_IP="${DISPLAY_IP:-192.168.42.98}"
CONTROLLER_BIN=".pio/build/esp32-furnace-controller/firmware.bin"
DISPLAY_BIN=".pio/build/esp32-furnace-thermostat/firmware.bin"

PASS=0
FAIL=0
RESULTS=()

log() { echo "[$(date '+%H:%M:%S')] $*"; }
log_pass() { log "  PASS $*"; }
log_fail() { log "  FAIL $*"; }

# Wait for a device to come back online after OTA reboot.
# Polls /status until uptime_ms < threshold, confirming a reboot happened.
wait_for_reboot() {
  local ip=$1
  local old_uptime_ms=$2
  local deadline=$((SECONDS + 90))
  log "    Waiting for $ip to reboot (old uptime ${old_uptime_ms}ms)..."
  while [[ $SECONDS -lt $deadline ]]; do
    sleep 3
    local new_uptime
    new_uptime=$(curl -sf --max-time 5 "http://$ip/status" 2>/dev/null \
                 | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('uptime_ms',999999999))" 2>/dev/null || echo "unreachable")
    if [[ "$new_uptime" == "unreachable" ]]; then
      log "    $ip unreachable (rebooting)..."
      continue
    fi
    if [[ "$new_uptime" -lt "$old_uptime_ms" ]]; then
      log "    $ip back online, new uptime=${new_uptime}ms"
      return 0
    fi
  done
  log "    TIMEOUT: $ip did not reboot within 90s (last uptime=$new_uptime)"
  return 1
}

# Flash one device via OTA, verify it reboots and comes back.
flash_device() {
  local name=$1
  local ip=$2
  local bin=$3

  if [[ ! -f "$bin" ]]; then
    log_fail "$name: firmware binary not found: $bin"
    return 1
  fi

  # Get current uptime before flashing
  local uptime_before
  uptime_before=$(curl -sf --max-time 5 "http://$ip/status" 2>/dev/null \
                  | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('uptime_ms',0))" 2>/dev/null || echo 0)
  log "  $name ($ip): uptime before=${uptime_before}ms, binary=$(du -h "$bin" | cut -f1)"

  # Run the OTA upload, capture HTTP response and curl exit code
  local http_out
  local curl_exit=0
  http_out=$(curl -sf --max-time 120 \
    -F "firmware=@$bin" \
    "http://$ip/update" 2>&1) || curl_exit=$?

  if [[ $curl_exit -ne 0 ]]; then
    log_fail "$name: curl failed (exit=$curl_exit): $http_out"
    return 1
  fi

  log "  $name: upload complete, response: ${http_out:0:80}"

  # Confirm reboot happened
  if wait_for_reboot "$ip" "$uptime_before"; then
    return 0
  else
    log_fail "$name: device did not reboot after OTA"
    return 1
  fi
}

# Run one round of flashing for the requested targets
run_round() {
  local round=$1
  local ok=true

  log "=== Round $round / $ITERATIONS ==="

  if [[ "$TARGET" == "controller" || "$TARGET" == "all" ]]; then
    if flash_device "Controller" "$CONTROLLER_IP" "$CONTROLLER_BIN"; then
      log_pass "Controller round $round"
    else
      log_fail "Controller round $round"
      ok=false
    fi
  fi

  if [[ "$TARGET" == "display" || "$TARGET" == "all" ]]; then
    if flash_device "Display" "$DISPLAY_IP" "$DISPLAY_BIN"; then
      log_pass "Display round $round"
    else
      log_fail "Display round $round"
      ok=false
    fi
  fi

  if $ok; then
    PASS=$((PASS + 1))
    RESULTS+=("Round $round: PASS")
  else
    FAIL=$((FAIL + 1))
    RESULTS+=("Round $round: FAIL")
  fi
}

# ---- Main ----
log "OTA soak test: $ITERATIONS iterations, target=$TARGET"
log "Controller: $CONTROLLER_IP  Display: $DISPLAY_IP"

for i in $(seq 1 "$ITERATIONS"); do
  run_round "$i"
  # Brief pause between rounds so devices can finish settling
  if [[ $i -lt $ITERATIONS ]]; then
    sleep 5
  fi
done

log ""
log "=== Results ==="
for r in "${RESULTS[@]}"; do log "  $r"; done
log "  Total: $PASS passed, $FAIL failed out of $ITERATIONS rounds"

[[ $FAIL -eq 0 ]]
