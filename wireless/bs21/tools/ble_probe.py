#!/usr/bin/env python3
"""BLE probe for Flydigi Apex 5 (Bluetooth mode).

Connects as a BLE central, enumerates all GATT services/characteristics,
subscribes to notifications/indications, reads every readable characteristic,
and replays the Vader-5 style 5a a5 init+enable frames to every writable
characteristic while logging all controller responses.

This is a discovery aid: the goal is to learn the real Flydigi V2 handshake
(bytes + which characteristic) so it can be ported to the SLE receiver.

Usage:
    python3 ble_probe.py [name_filter] [mac]
Example:
    python3 ble_probe.py apex
    python3 ble_probe.py "" AA:BB:CC:DD:EE:FF
"""
import asyncio
import datetime
import sys

from bleak import BleakScanner, BleakClient

V2_INIT = [
    bytes([0x5A, 0xA5, 0x01, 0x02, 0x03]),
    bytes([0x5A, 0xA5, 0xA1, 0x02, 0xA3]),
    bytes([0x5A, 0xA5, 0x02, 0x02, 0x04]),
    bytes([0x5A, 0xA5, 0x04, 0x02, 0x06]),
]
V2_ENABLE = bytes([0x5A, 0xA5, 0x11, 0x07, 0xFF, 0x01, 0xFF, 0xFF, 0xFF, 0x15])

notif_log = []


def log(msg):
    ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{ts}] {msg}", flush=True)


def make_handler(uuid):
    def handler(_sender, data):
        notif_log.append((str(uuid), bytes(data)))
        log(f"NOTIFY {uuid}: {bytes(data).hex(' ')}")
    return handler


async def main():
    name_filter = sys.argv[1] if len(sys.argv) > 1 else None
    mac = sys.argv[2] if len(sys.argv) > 2 else None
    if name_filter == "":
        name_filter = None

    log("scanning (8s)...")
    devices = await BleakScanner.discover(timeout=8)
    target = None
    for d in devices:
        nm = d.name or ""
        log(f"  found {d.address} name={nm!r} rssi={d.rssi}")
        if mac and d.address.lower() == mac.lower():
            target = d
        elif name_filter and name_filter.lower() in nm.lower():
            target = d
    if target is None:
        log("NO MATCH. Re-run with the controller's MAC, or relax the name filter.")
        return

    log(f"connecting to {target.address} name={target.name!r}")
    async with BleakClient(target.address) as client:
        log("connected")
        svcs = await client.get_services()
        notify_chars = []
        for s in svcs:
            log(f"SERVICE {s.uuid} ({s.description})")
            for c in s.characteristics:
                props = ",".join(c.properties)
                log(f"  CHAR {c.uuid} h={c.handle} props={props} desc={c.description}")
                if "notify" in c.properties or "indicate" in c.properties:
                    try:
                        await client.start_notify(c, make_handler(c.uuid))
                        notify_chars.append(c)
                    except Exception as e:  # noqa: BLE001
                        log(f"    start_notify fail: {e}")
        log(f"subscribed to {len(notify_chars)} notify/indicate characteristic(s)")

        for s in svcs:
            for c in s.characteristics:
                if "read" in c.properties:
                    try:
                        v = await client.read_gatt_char(c)
                        log(f"  READ {c.uuid}: {bytes(v).hex(' ')}")
                    except Exception as e:  # noqa: BLE001
                        log(f"  READ {c.uuid} fail: {e}")

        for s in svcs:
            for c in s.characteristics:
                if "write" in c.properties or "write-without-response" in c.properties:
                    need_rsp = "write" in c.properties
                    for frame in V2_INIT + [V2_ENABLE]:
                        try:
                            await client.write_gatt_char(c, frame, response=need_rsp)
                            log(f"  WROTE {c.uuid}: {frame.hex(' ')}")
                            await asyncio.sleep(0.05)
                        except Exception as e:  # noqa: BLE001
                            log(f"  WRITE {c.uuid} fail: {e}")
        log("waiting 3s for any stream...")
        await asyncio.sleep(3)
        log(f"done. notifications captured: {len(notif_log)}")


if __name__ == "__main__":
    asyncio.run(main())
