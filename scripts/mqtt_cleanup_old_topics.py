#!/usr/bin/env python3
"""Clean up retained MQTT messages from old topic structure.

Clears retained messages from:
  thermostat/furnace-controller/#
  thermostat/furnace-display/#
  wireless_thermostat_system/#

Usage:
  python3 scripts/mqtt_cleanup_old_topics.py --host mqtt.lan
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import paho.mqtt.client as mqtt
except Exception as exc:  # pragma: no cover
    print("error: paho-mqtt is required (pip install paho-mqtt)", file=sys.stderr)
    raise SystemExit(2) from exc

OLD_PREFIXES = [
    "thermostat/furnace-controller/#",
    "thermostat/furnace-display/#",
    "wireless_thermostat_system/#",
]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Clear retained MQTT messages from old topic prefixes"
    )
    parser.add_argument("--host", default="mqtt.lan")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--wait", type=float, default=5.0,
                        help="Seconds to wait for retained messages (default: 5)")
    parser.add_argument("--dry-run", action="store_true",
                        help="List retained topics without clearing them")
    args = parser.parse_args()

    retained_topics: set[str] = set()

    def on_connect(client, _userdata, _flags, rc, _props=None):
        if rc != 0:
            print(f"connect failed rc={rc}", file=sys.stderr)
            return
        for pattern in OLD_PREFIXES:
            client.subscribe(pattern, qos=1)
            print(f"  subscribed: {pattern}")

    def on_message(_client, _userdata, msg):
        # Only collect retained messages
        if msg.retain and msg.payload:
            retained_topics.add(msg.topic)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="mqtt-cleanup-old")
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"Connecting to {args.host}:{args.port} ...")
    client.connect(args.host, args.port, keepalive=30)
    client.loop_start()

    print(f"Waiting {args.wait}s for retained messages ...")
    time.sleep(args.wait)

    if not retained_topics:
        print("No retained messages found on old topics. Nothing to clean up.")
        client.loop_stop()
        client.disconnect()
        return 0

    print(f"\nFound {len(retained_topics)} retained topic(s):")
    for topic in sorted(retained_topics):
        print(f"  {topic}")

    if args.dry_run:
        print("\n--dry-run: no messages cleared.")
        client.loop_stop()
        client.disconnect()
        return 0

    print("\nClearing retained messages ...")
    cleared = 0
    for topic in sorted(retained_topics):
        # Publishing an empty retained message clears the retained value
        client.publish(topic, b"", qos=1, retain=True)
        cleared += 1

    # Allow publishes to flush
    time.sleep(1)

    print(f"Cleared {cleared} retained message(s).")
    client.loop_stop()
    client.disconnect()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
