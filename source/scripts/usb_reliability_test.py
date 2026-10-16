"""USB link reliability test for the Krteq raw HID interface.

Sends random packets and verifies the echo. VIA replies to any unknown command
id with the same buffer, only byte 0 replaced by 0xFF, so no firmware changes
are needed.

Requires: pip install hidapi
"""

import json
import os
import statistics
import time
from collections import Counter
from pathlib import Path

import hid

# Config
DURATION_S = 30
INTERVAL_MS = 5
TIMEOUT_MS = 1000

# VIA raw HID
USAGE_PAGE = 0xFF60
USAGE = 0x61
REPORT_SIZE = 32
PING_ID = 0xAA  # Not implemented by VIA, so it gets echoed
ECHO_ID = 0xFF

keyboard_json = Path(__file__).resolve().parents[1] / "qmk" / "keyboard.json"
usb = json.loads(keyboard_json.read_text())["usb"]
VID = int(usb["vid"], 16)
PID = int(usb["pid"], 16)


def log(message):
    print(f"\n[{time.strftime('%H:%M:%S')}] {message}")


def ms_since(start):
    return (time.perf_counter() - start) * 1000


def connect():
    for info in hid.enumerate(VID, PID):
        if info["usage_page"] == USAGE_PAGE and info["usage"] == USAGE:
            dev = hid.device()
            dev.open_path(info["path"])
            return dev
    return None


def reconnect(deadline):
    while time.perf_counter() < deadline:
        time.sleep(0.25)
        try:
            if dev := connect():
                return dev
        except OSError:
            pass
    return None


def ping(dev, seq):
    payload = bytes([PING_ID]) + seq.to_bytes(4, "big") + os.urandom(REPORT_SIZE - 5)
    start = time.perf_counter()
    dev.write(b"\x00" + payload)  # Leading report id, required on Windows

    while (remaining := TIMEOUT_MS - ms_since(start)) > 0:
        reply = bytes(dev.read(REPORT_SIZE, int(remaining)))
        if not reply:
            break
        if len(reply) != REPORT_SIZE or reply[0] != ECHO_ID:
            return "malformed", None
        if reply[1:5] == payload[1:5]:  # Otherwise a stale reply to an earlier packet
            return ("ok" if reply[5:] == payload[5:] else "corrupt"), ms_since(start)

    return "timeout", None


dev = connect()
if dev is None:
    raise SystemExit(f"No raw HID interface found for {VID:#06x}:{PID:#06x}. Is the keyboard connected with VIA enabled?")

print(f"Connected to {VID:#06x}:{PID:#06x}, testing for {DURATION_S}s. Ctrl+C to stop early.")

counts = Counter()
latencies = []
drops = 0
seq = 0
deadline = time.perf_counter() + DURATION_S

try:
    while dev and time.perf_counter() < deadline:
        seq += 1
        try:
            result, latency = ping(dev, seq)
        except (OSError, ValueError) as e:
            log(f"Link dropped: {e}")
            drops += 1
            dev.close()
            if dev := reconnect(deadline):
                log("Reconnected")
            continue

        counts[result] += 1
        if result == "ok":
            latencies.append(latency)
        else:
            log(f"Packet {seq}: {result}")

        if seq % 20 == 0:
            avg = statistics.fmean(latencies) if latencies else 0
            print(f"\rsent {counts.total()}  ok {counts['ok']}  avg {avg:6.2f} ms", end="", flush=True)

        time.sleep(INTERVAL_MS / 1000)
except KeyboardInterrupt:
    pass

total = counts.total()
failed = total - counts["ok"]

print("\n")
print(f"Packets sent      {total}")
print(f"Echoed correctly  {counts['ok']}")
print(f"Timeouts          {counts['timeout']}")
print(f"Corrupt payloads  {counts['corrupt']}")
print(f"Malformed replies {counts['malformed']}")
print(f"Link drops        {drops}")

if total:
    print(f"Failure rate      {failed / total * 100:.3f} %")

if latencies:
    latencies.sort()
    print()
    print(f"Latency min       {latencies[0]:.2f} ms")
    print(f"Latency median    {statistics.median(latencies):.2f} ms")
    print(f"Latency p99       {latencies[int((len(latencies) - 1) * 0.99)]:.2f} ms")
    print(f"Latency max       {latencies[-1]:.2f} ms")

print("\nLink looks healthy." if failed == 0 and drops == 0 else "\nLink errors detected.")
