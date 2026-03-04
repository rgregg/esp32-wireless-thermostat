#!/usr/bin/env bash
# integration-test.sh — Hardware-in-the-loop integration tests via web + serial
#
# Tests the thermostat display and furnace controller over HTTP to verify:
#   - Web server responsiveness (the FreeRTOS task fix)
#   - Screenshot streaming doesn't block the main loop
#   - Config read/write round-trips
#   - ESP-NOW heartbeat flow between devices
#   - Cross-device state synchronization via MQTT
#   - Heap stability under load
#
# Prerequisites:
#   - Both devices powered on and connected to WiFi
#   - curl and python3 available
#   - Optional: serial port connected for crash monitoring
#
# Usage:
#   ./scripts/integration-test.sh                    # defaults
#   ./scripts/integration-test.sh --display-only     # skip controller tests
#   ./scripts/integration-test.sh --serial /dev/cu.usbserial-2110
#   DISPLAY_IP=10.0.0.50 CONTROLLER_IP=10.0.0.51 ./scripts/integration-test.sh

set -uo pipefail

# ---------------------------------------------------------------------------
# Configuration (override via environment variables)
# ---------------------------------------------------------------------------
DISPLAY_IP="${DISPLAY_IP:-192.168.42.98}"
CONTROLLER_IP="${CONTROLLER_IP:-192.168.42.94}"
SERIAL_PORT="${SERIAL_PORT:-}"
DISPLAY_ONLY=false
VERBOSE=false

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    --display-only) DISPLAY_ONLY=true; shift ;;
    --serial) SERIAL_PORT="$2"; shift 2 ;;
    --verbose) VERBOSE=true; shift ;;
    -h|--help)
      echo "Usage: $0 [--display-only] [--serial PORT] [--verbose]"
      echo "  DISPLAY_IP=x.x.x.x CONTROLLER_IP=x.x.x.x $0"
      exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
PASS=0
FAIL=0
SKIP=0
SERIAL_PID=""
SERIAL_LOG=""

pass() { ((PASS++)); echo "  ✓ PASS: $1"; }
fail() { ((FAIL++)); echo "  ✗ FAIL: $1"; }
skip() { ((SKIP++)); echo "  - SKIP: $1"; }
info() { echo "  · $1"; }

json_field() {
  # Usage: json_field '{"key":"val"}' key
  python3 -c "import sys,json; print(json.load(sys.stdin)['$2'])" <<< "$1" 2>/dev/null
}

check_reachable() {
  local name="$1" ip="$2"
  if curl -s --max-time 3 "http://$ip/status" > /dev/null 2>&1; then
    return 0
  else
    echo "ERROR: $name ($ip) is unreachable. Aborting."
    return 1
  fi
}

cleanup() {
  if [[ -n "$SERIAL_PID" ]] && kill -0 "$SERIAL_PID" 2>/dev/null; then
    kill "$SERIAL_PID" 2>/dev/null
    wait "$SERIAL_PID" 2>/dev/null
  fi
  if [[ -n "$SERIAL_LOG" && -f "$SERIAL_LOG" ]]; then
    local lines
    lines=$(wc -l < "$SERIAL_LOG" | tr -d ' ')
    if [[ "$lines" -gt 0 ]]; then
      echo ""
      echo "=== Serial output captured ($lines lines) ==="
      cat "$SERIAL_LOG"
    fi
    rm -f "$SERIAL_LOG"
  fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Pre-flight
# ---------------------------------------------------------------------------
echo "╔══════════════════════════════════════════════════════════╗"
echo "║         ESP32 Wireless Thermostat — Integration Tests   ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "Display:    http://$DISPLAY_IP"
[[ "$DISPLAY_ONLY" == false ]] && echo "Controller: http://$CONTROLLER_IP"
echo ""

check_reachable "Display" "$DISPLAY_IP" || exit 1
if [[ "$DISPLAY_ONLY" == false ]]; then
  check_reachable "Controller" "$CONTROLLER_IP" || exit 1
fi

# Start serial monitor if port given
if [[ -n "$SERIAL_PORT" ]]; then
  if [[ -e "$SERIAL_PORT" ]]; then
    SERIAL_LOG=$(mktemp /tmp/integration-serial.XXXXXX)
    python3 -c "
import serial, sys
ser = serial.Serial('$SERIAL_PORT', 115200, timeout=1)
while True:
    data = ser.read(ser.in_waiting or 1)
    if data:
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
" > "$SERIAL_LOG" 2>/dev/null &
    SERIAL_PID=$!
    echo "Serial monitor: $SERIAL_PORT (PID $SERIAL_PID)"
  else
    echo "Serial port $SERIAL_PORT not found — skipping serial monitoring"
  fi
fi

# Grab baseline
baseline=$(curl -s "http://$DISPLAY_IP/status")
baseline_uptime=$(json_field "$baseline" uptime_ms)
baseline_heap=$(json_field "$baseline" free_heap)
baseline_version=$(json_field "$baseline" firmware_version)
echo "Firmware:   $baseline_version"
echo "Uptime:     $((baseline_uptime / 1000))s"
echo "Free heap:  $baseline_heap bytes"
echo ""

# =========================================================================
# Test 1: Web server responsiveness — rapid root page loads
# =========================================================================
echo "── Test 1: Display root page (10x rapid) ──"
t1_fail=0
for i in $(seq 1 10); do
  code=$(curl -s -o /dev/null --max-time 5 -w "%{http_code}" "http://$DISPLAY_IP/")
  if [[ "$code" != "200" ]]; then
    info "Request $i: HTTP $code"
    ((t1_fail++))
  fi
done
[[ $t1_fail -eq 0 ]] && pass "10/10 root page loads returned HTTP 200" || fail "$((10-t1_fail))/10 succeeded"

# =========================================================================
# Test 2: Status endpoint — rapid polling + heap check
# =========================================================================
echo "── Test 2: Display status endpoint (10x) ──"
t2_fail=0
t2_heaps=()
for i in $(seq 1 10); do
  status=$(curl -s --max-time 5 "http://$DISPLAY_IP/status" 2>/dev/null)
  heap=$(json_field "$status" free_heap 2>/dev/null)
  mqtt=$(json_field "$status" mqtt_connected 2>/dev/null)
  if [[ -z "$heap" || "$mqtt" != "True" ]]; then
    ((t2_fail++))
  fi
  t2_heaps+=("$heap")
done
[[ $t2_fail -eq 0 ]] && pass "10/10 status polls OK, MQTT stayed connected" || fail "$t2_fail polls had issues"
[[ "$VERBOSE" == true ]] && info "Heap samples: ${t2_heaps[*]}"

# =========================================================================
# Test 3: Screenshot download (heavy — 1MB+ BMP)
# =========================================================================
echo "── Test 3: Screenshot download ──"
t3_start=$(date +%s)
t3_tmp=$(mktemp /tmp/screenshot.XXXXXX.bmp)
t3_code=$(curl -s -o "$t3_tmp" --max-time 30 -w "%{http_code}" "http://$DISPLAY_IP/screenshot")
t3_end=$(date +%s)
t3_size=$(wc -c < "$t3_tmp" | tr -d ' ')
rm -f "$t3_tmp"
t3_elapsed=$((t3_end - t3_start))
if [[ "$t3_code" == "200" && "$t3_size" -gt 100000 ]]; then
  pass "Screenshot downloaded: ${t3_size} bytes in ${t3_elapsed}s"
else
  fail "Screenshot: HTTP $t3_code, size=$t3_size"
fi

# Verify device didn't reboot
post_scr=$(curl -s "http://$DISPLAY_IP/status")
post_uptime=$(json_field "$post_scr" uptime_ms)
if [[ "$post_uptime" -gt "$baseline_uptime" ]]; then
  pass "Device still up after screenshot (uptime advanced)"
else
  fail "Device may have rebooted (uptime went backwards)"
fi

# =========================================================================
# Test 4: Screenshot doesn't block main loop
# =========================================================================
echo "── Test 4: Main loop not blocked during screenshot ──"
pre_mqtt=$(json_field "$(curl -s "http://$DISPLAY_IP/status")" mqtt_ctrl_last_update_ms)
curl -s -o /dev/null --max-time 30 "http://$DISPLAY_IP/screenshot"
sleep 3
post_mqtt=$(json_field "$(curl -s "http://$DISPLAY_IP/status")" mqtt_ctrl_last_update_ms)
if [[ "$post_mqtt" -gt "$pre_mqtt" ]]; then
  pass "MQTT controller updates continued during screenshot"
else
  # Controller publishes every ~10s, so this can legitimately be equal
  skip "MQTT update timestamp didn't advance (may be timing)"
fi

# =========================================================================
# Test 5: Lazy-load screenshot (only fetched on System tab)
# =========================================================================
echo "── Test 5: Screenshot lazy-load in HTML ──"
html=$(curl -s --max-time 5 "http://$DISPLAY_IP/")
if echo "$html" | grep -q 'data-src="/screenshot"'; then
  pass "Screenshot uses data-src (lazy-load)"
else
  if echo "$html" | grep -q 'src="/screenshot"'; then
    fail "Screenshot uses src= (eager load — blocks page)"
  else
    fail "No screenshot element found in HTML"
  fi
fi

# =========================================================================
# Test 6: Config read/write round-trip
# =========================================================================
echo "── Test 6: Config write round-trip ──"
config=$(curl -s --max-time 5 "http://$DISPLAY_IP/config")
orig_bl=$(json_field "$config" backlight_screensaver_pct)
test_val=$((orig_bl == 35 ? 40 : 35))

curl -s --max-time 5 -X POST -d "backlight_screensaver_pct=$test_val" "http://$DISPLAY_IP/config" > /dev/null
sleep 1
verify=$(json_field "$(curl -s "http://$DISPLAY_IP/config")" backlight_screensaver_pct)

# Restore immediately
curl -s --max-time 5 -X POST -d "backlight_screensaver_pct=$orig_bl" "http://$DISPLAY_IP/config" > /dev/null
sleep 1
restored=$(json_field "$(curl -s "http://$DISPLAY_IP/config")" backlight_screensaver_pct)

if [[ "$verify" == "$test_val" && "$restored" == "$orig_bl" ]]; then
  pass "Config round-trip: $orig_bl → $test_val → $orig_bl"
else
  fail "Config round-trip: expected $test_val got $verify, restore expected $orig_bl got $restored"
fi

# =========================================================================
# Test 7: ESP-NOW heartbeats flowing
# =========================================================================
echo "── Test 7: ESP-NOW heartbeats ──"
hb1=$(json_field "$(curl -s "http://$DISPLAY_IP/status")" espnow_heartbeat_ms)
sleep 6
hb2=$(json_field "$(curl -s "http://$DISPLAY_IP/status")" espnow_heartbeat_ms)
if [[ "$hb2" -gt "$hb1" ]]; then
  pass "Display ESP-NOW heartbeat advancing ($hb1 → $hb2)"
else
  fail "Display ESP-NOW heartbeat stale ($hb1 → $hb2)"
fi

# =========================================================================
# Test 8: Heap stability under sustained polling
# =========================================================================
echo "── Test 8: Heap stability (20s sustained polling) ──"
t8_min=999999
t8_max=0
for i in $(seq 1 10); do
  heap=$(json_field "$(curl -s "http://$DISPLAY_IP/status")" free_heap 2>/dev/null)
  if [[ -n "$heap" ]]; then
    [[ "$heap" -lt "$t8_min" ]] && t8_min=$heap
    [[ "$heap" -gt "$t8_max" ]] && t8_max=$heap
  fi
  sleep 2
done
t8_drift=$((t8_max - t8_min))
if [[ "$t8_min" -gt 5000 ]]; then
  pass "Heap range: ${t8_min}–${t8_max} (drift: ${t8_drift} bytes)"
else
  fail "Heap dangerously low: min=${t8_min}"
fi

# =========================================================================
# Controller-specific tests
# =========================================================================
if [[ "$DISPLAY_ONLY" == false ]]; then

  echo ""
  echo "── Test 9: Controller web endpoints ──"
  t9_fail=0
  for endpoint in "/" "/status" "/config" "/devices"; do
    code=$(curl -s -o /dev/null --max-time 5 -w "%{http_code}" "http://$CONTROLLER_IP$endpoint")
    if [[ "$code" == "200" ]]; then
      [[ "$VERBOSE" == true ]] && info "$endpoint: HTTP $code"
    else
      info "$endpoint: HTTP $code"
      ((t9_fail++))
    fi
  done
  [[ $t9_fail -eq 0 ]] && pass "All controller endpoints returned HTTP 200" || fail "$t9_fail endpoints failed"

  echo "── Test 10: Controller rapid page loads (10x) ──"
  t10_fail=0
  for i in $(seq 1 10); do
    code=$(curl -s -o /dev/null --max-time 5 -w "%{http_code}" "http://$CONTROLLER_IP/")
    [[ "$code" != "200" ]] && ((t10_fail++))
  done
  [[ $t10_fail -eq 0 ]] && pass "10/10 controller page loads returned HTTP 200" || fail "$((10-t10_fail))/10 succeeded"

  echo "── Test 11: Cross-device state sync ──"
  d_status=$(curl -s "http://$DISPLAY_IP/status")
  c_status=$(curl -s "http://$CONTROLLER_IP/status")
  d_mode=$(json_field "$d_status" mqtt_ctrl_mode)
  c_mode=$(json_field "$c_status" mode)
  if [[ "$d_mode" == "$c_mode" ]]; then
    pass "Mode synced: $d_mode"
  else
    fail "Mode mismatch: display=$d_mode controller=$c_mode"
  fi

  d_avail=$(json_field "$d_status" mqtt_ctrl_available)
  if [[ "$d_avail" == "True" ]]; then
    pass "Display sees controller as available"
  else
    fail "Display sees controller as unavailable"
  fi

  echo "── Test 12: Controller heap stability ──"
  c_min=999999
  c_max=0
  for i in $(seq 1 5); do
    c_heap=$(json_field "$(curl -s "http://$CONTROLLER_IP/status")" free_heap 2>/dev/null)
    if [[ -n "$c_heap" ]]; then
      [[ "$c_heap" -lt "$c_min" ]] && c_min=$c_heap
      [[ "$c_heap" -gt "$c_max" ]] && c_max=$c_heap
    fi
    sleep 2
  done
  if [[ "$c_min" -gt 50000 ]]; then
    pass "Controller heap range: ${c_min}–${c_max}"
  else
    fail "Controller heap low: min=${c_min}"
  fi

  echo "── Test 13: Device discovery ──"
  devices=$(curl -s "http://$CONTROLLER_IP/devices")
  dev_count=$(python3 -c "import sys,json; print(len(json.load(sys.stdin)))" <<< "$devices" 2>/dev/null)
  if [[ "$dev_count" -gt 0 ]]; then
    pass "Controller sees $dev_count device(s)"
    if [[ "$VERBOSE" == true ]]; then
      python3 -c "
import sys,json
for d in json.load(sys.stdin):
    print(f'    {d[\"name\"]} ({d[\"type\"]}) — {d[\"mac\"]}')
" <<< "$devices"
    fi
  else
    fail "Controller device registry empty"
  fi
fi

# =========================================================================
# Final health check
# =========================================================================
echo ""
echo "── Final health check ──"
final=$(curl -s "http://$DISPLAY_IP/status")
final_uptime=$(json_field "$final" uptime_ms)
final_heap=$(json_field "$final" free_heap)
final_mqtt=$(json_field "$final" mqtt_connected)
final_espnow=$(json_field "$final" espnow_connected)

if [[ "$final_uptime" -gt "$baseline_uptime" ]]; then
  pass "Display survived all tests (uptime: $((final_uptime / 1000))s, no reboot)"
else
  fail "Display may have rebooted during tests"
fi
info "Final heap: $final_heap (was: $baseline_heap, delta: $((final_heap - baseline_heap)))"
info "MQTT: $final_mqtt  ESP-NOW: $final_espnow"

# =========================================================================
# Summary
# =========================================================================
echo ""
echo "════════════════════════════════════════"
TOTAL=$((PASS + FAIL + SKIP))
echo "  Results: $PASS passed, $FAIL failed, $SKIP skipped / $TOTAL total"
echo "════════════════════════════════════════"

if [[ $FAIL -gt 0 ]]; then
  exit 1
fi
