#!/usr/bin/env python3
"""Broker-backed MQTT path smoke test for thermostat/controller pair.

Validates:
- thermostat granular commands produce packed command mirror with correct encoding
- controller follows display packed command mirror (two state changes)
- end-to-end round-trip: display cmd → packed_command/<MAC> → controller state

Usage:
  python3 scripts/mqtt_path_smoke.py --host mqtt.lan
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from typing import Dict

try:
    import paho.mqtt.client as mqtt
except Exception as exc:  # pragma: no cover
    print("error: paho-mqtt is required (pip install paho-mqtt)", file=sys.stderr)
    raise SystemExit(2) from exc


@dataclass
class CommandFields:
    mode: int
    fan: int
    setpoint_decic: int
    seq: int
    filter_reset: bool
    sync_request: bool


def decode_command(word: int) -> CommandFields:
    return CommandFields(
        mode=(word >> 0) & 0x3,
        fan=(word >> 2) & 0x3,
        setpoint_decic=min(400, (word >> 4) & 0x1FF),
        seq=(word >> 13) & 0x1FF,
        filter_reset=((word >> 22) & 0x1) != 0,
        sync_request=((word >> 23) & 0x1) != 0,
    )


def wait_for(predicate, timeout_s: float, step_s: float = 0.05) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(step_s)
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="mqtt.lan")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--prefix", default="thermostat")
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()

    ctrl_base = f"{args.prefix}/furnace-controller"
    disp_base = f"{args.prefix}/furnace-display"

    want_topics = [
        f"{ctrl_base}/state/availability",
        f"{ctrl_base}/state/mode",
        f"{ctrl_base}/state/fan_mode",
        f"{ctrl_base}/state/target_temp_c",
        f"{disp_base}/state/availability",
        f"{disp_base}/state/packed_command/+",  # firmware appends /<MAC>
    ]

    packed_cmd_prefix = f"{disp_base}/state/packed_command/"

    latest: Dict[str, str] = {}

    def on_connect(client, _userdata, _flags, rc, _props=None):
        if rc != 0:
            print(f"connect failed rc={rc}", file=sys.stderr)
            return
        for t in want_topics:
            client.subscribe(t, qos=1)

    def on_message(_client, _userdata, msg):
        payload = msg.payload.decode("utf-8", errors="ignore").strip()
        # Normalize packed_command/<MAC> to packed_command for lookup
        if msg.topic.startswith(packed_cmd_prefix) and payload:
            latest[f"{disp_base}/state/packed_command"] = payload
        else:
            latest[msg.topic] = payload

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="mqtt-path-smoke")
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(args.host, args.port, keepalive=30)
    client.loop_start()

    try:
        ok = wait_for(
            lambda: latest.get(f"{ctrl_base}/state/availability") == "online"
            and latest.get(f"{disp_base}/state/availability") == "online",
            args.timeout,
        )
        if not ok:
            print("error: did not observe both controller and display online", file=sys.stderr)
            return 1

        # Drive thermostat via HA-style granular commands (non-retained to
        # avoid leaving stale commands on the broker after the test).
        client.publish(f"{disp_base}/cmd/mode", "heat", qos=1, retain=False)
        client.publish(f"{disp_base}/cmd/fan_mode", "circulate", qos=1, retain=False)
        client.publish(f"{disp_base}/cmd/target_temp_c", "21.5", qos=1, retain=False)

        def packed_matches_expected(mode: int, fan: int, setpoint: int) -> bool:
            raw = latest.get(f"{disp_base}/state/packed_command")
            if not raw:
                return False
            decoded = decode_command(int(raw))
            return decoded.mode == mode and decoded.fan == fan and decoded.setpoint_decic == setpoint

        ok = wait_for(lambda: packed_matches_expected(1, 2, 215), args.timeout)
        if not ok:
            raw = latest.get(f"{disp_base}/state/packed_command", "missing")
            if raw and raw != "missing":
                d = decode_command(int(raw))
                print(
                    f"error: packed command mismatch mode={d.mode} fan={d.fan} setpoint={d.setpoint_decic}",
                    file=sys.stderr,
                )
            else:
                print("error: packed command mirror was not published by thermostat", file=sys.stderr)
            return 1

        ok = wait_for(
            lambda: latest.get(f"{ctrl_base}/state/mode") == "heat"
            and latest.get(f"{ctrl_base}/state/fan_mode") == "circulate",
            args.timeout,
        )
        if not ok:
            print("error: controller did not follow thermostat packed command", file=sys.stderr)
            return 1

        # Drive a second state change through the display to verify the full
        # round-trip: display cmd → packed_command/<MAC> → controller applies.
        client.publish(f"{disp_base}/cmd/mode", "cool", qos=1, retain=False)
        client.publish(f"{disp_base}/cmd/fan_mode", "on", qos=1, retain=False)
        client.publish(f"{disp_base}/cmd/target_temp_c", "19.0", qos=1, retain=False)

        ok = wait_for(
            lambda: latest.get(f"{ctrl_base}/state/mode") == "cool"
            and latest.get(f"{ctrl_base}/state/fan_mode") == "on"
            and latest.get(f"{ctrl_base}/state/target_temp_c") == "19.0",
            args.timeout,
        )
        if not ok:
            print("error: controller did not follow second display command set", file=sys.stderr)
            return 1

        print("PASS: MQTT path smoke checks succeeded")
        return 0
    finally:
        # Best-effort restore: send heat/auto/20.0 to the display.  The
        # display's packed-command heartbeat will propagate to the controller
        # once its sequence counter advances past the controller's tracker.
        client.publish(f"{disp_base}/cmd/mode", "heat", qos=1, retain=False)
        client.publish(f"{disp_base}/cmd/fan_mode", "auto", qos=1, retain=False)
        client.publish(f"{disp_base}/cmd/target_temp_c", "20.0", qos=1, retain=False)
        time.sleep(1)
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    raise SystemExit(main())
