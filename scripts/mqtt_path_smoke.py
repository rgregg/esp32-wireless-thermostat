#!/usr/bin/env python3
"""Broker-backed MQTT path smoke test for thermostat/controller pair.

Validates:
- thermostat granular commands produce packed command mirror
- controller follows packed command mirror
- controller direct packed command endpoint works

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


def encode_command(fields: CommandFields) -> int:
    sp = max(0, min(400, fields.setpoint_decic))
    seq = fields.seq & 0x1FF
    w = 0
    w |= (fields.mode & 0x3) << 0
    w |= (fields.fan & 0x3) << 2
    w |= (sp & 0x1FF) << 4
    w |= (seq & 0x1FF) << 13
    w |= (1 if fields.filter_reset else 0) << 22
    w |= (1 if fields.sync_request else 0) << 23
    return w


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
        f"{disp_base}/state/packed_command",
    ]

    latest: Dict[str, str] = {}

    def on_connect(client, _userdata, _flags, rc, _props=None):
        if rc != 0:
            print(f"connect failed rc={rc}", file=sys.stderr)
            return
        for t in want_topics:
            client.subscribe(t, qos=1)

    def on_message(_client, _userdata, msg):
        latest[msg.topic] = msg.payload.decode("utf-8", errors="ignore").strip()

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

        # Drive thermostat via HA-style granular commands.
        client.publish(f"{disp_base}/cmd/mode", "heat", qos=1, retain=True)
        client.publish(f"{disp_base}/cmd/fan_mode", "circulate", qos=1, retain=True)
        client.publish(f"{disp_base}/cmd/target_temp_c", "21.5", qos=1, retain=True)

        ok = wait_for(lambda: f"{disp_base}/state/packed_command" in latest, args.timeout)
        if not ok:
            print("error: packed command mirror was not published by thermostat", file=sys.stderr)
            return 1

        packed = int(latest[f"{disp_base}/state/packed_command"])
        decoded = decode_command(packed)
        if decoded.mode != 1 or decoded.fan != 2 or decoded.setpoint_decic != 215:
            print(
                f"error: packed command mismatch mode={decoded.mode} fan={decoded.fan} setpoint={decoded.setpoint_decic}",
                file=sys.stderr,
            )
            return 1

        ok = wait_for(
            lambda: latest.get(f"{ctrl_base}/state/mode") == "heat"
            and latest.get(f"{ctrl_base}/state/fan_mode") == "circulate",
            args.timeout,
        )
        if not ok:
            print("error: controller did not follow thermostat packed command", file=sys.stderr)
            return 1

        # Direct packed command to controller path.
        word = encode_command(
            CommandFields(mode=2, fan=1, setpoint_decic=190, seq=(decoded.seq + 1) & 0x1FF,
                          filter_reset=False, sync_request=False)
        )
        client.publish(f"{ctrl_base}/cmd/packed_word", str(word), qos=1, retain=False)

        ok = wait_for(
            lambda: latest.get(f"{ctrl_base}/state/mode") == "cool"
            and latest.get(f"{ctrl_base}/state/fan_mode") == "on",
            args.timeout,
        )
        if not ok:
            print("error: controller direct packed command path did not apply", file=sys.stderr)
            return 1

        print("PASS: MQTT path smoke checks succeeded")
        return 0
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    raise SystemExit(main())
