"""USB link reliability test for a QMK/VIA keyboard.

Sends packets to the raw HID interface and verifies the echo. VIA replies to any
unrecognized command id with the same buffer, only byte 0 replaced by 0xFF, so
this works without any firmware changes.

Requires: pip install hidapi
"""

import argparse
import os
import statistics
import sys
import time

try:
    import hid
except ImportError:
    print("Missing dependency. Run: pip install hidapi")
    raise SystemExit(1)

# VIA raw HID interface
USAGE_PAGE = 0xFF60
USAGE = 0x61
REPORT_SIZE = 32

# Any command id VIA doesn't implement, so the packet is echoed back
PING_ID = 0xAA
UNHANDLED_ID = 0xFF

args_parse = argparse.ArgumentParser(description=__doc__)
args_parse.add_argument("--vid", type=lambda v: int(v, 0), default=0x4A42)
args_parse.add_argument("--pid", type=lambda v: int(v, 0), default=0x1617)
args_parse.add_argument("--duration", type=float, default=30.0, help="test length in seconds")
args_parse.add_argument("--interval", type=float, default=5.0, help="delay between packets in ms, 0 for max rate")
args_parse.add_argument("--timeout", type=float, default=1000.0, help="reply timeout in ms")
args = args_parse.parse_args()


def find_device(vid, pid):
    for info in hid.enumerate(vid, pid):
        if info.get("usage_page") == USAGE_PAGE and info.get("usage") == USAGE:
            return info["path"]
    return None


def open_device(path):
    # hidapi exposes two incompatible APIs depending on the package build
    if hasattr(hid, "Device"):
        return hid.Device(path=path)
    dev = hid.device()
    dev.open_path(path)
    return dev


def ping(dev, seq, timeout_ms):
    payload = bytes([PING_ID]) + seq.to_bytes(4, "big") + os.urandom(REPORT_SIZE - 5)

    start = time.perf_counter()
    dev.write(b"\x00" + payload)  # leading report id, required on Windows

    while True:
        remaining = timeout_ms - (time.perf_counter() - start) * 1000
        if remaining <= 0:
            return "timeout", None

        reply = bytes(dev.read(REPORT_SIZE, int(remaining)) or b"")
        elapsed = (time.perf_counter() - start) * 1000

        if not reply:
            return "timeout", None
        if len(reply) != REPORT_SIZE or reply[0] != UNHANDLED_ID:
            return "malformed", elapsed
        if reply[1:5] != payload[1:5]:
            continue  # stale reply from an earlier packet, keep waiting
        if reply[5:] != payload[5:]:
            return "corrupt", elapsed

        return "ok", elapsed


path = find_device(args.vid, args.pid)
if path is None:
    print(f"No raw HID interface found for {args.vid:#06x}:{args.pid:#06x}.")
    print("Check the keyboard is connected and that VIA/raw HID is enabled in the firmware.")
    raise SystemExit(1)

dev = open_device(path)
print(f"Connected to {args.vid:#06x}:{args.pid:#06x}, testing for {args.duration:g}s. Ctrl+C to stop early.\n")

latencies = []
counts = {"ok": 0, "timeout": 0, "corrupt": 0, "malformed": 0}
drops = 0
seq = 0
deadline = time.perf_counter() + args.duration

try:
    while time.perf_counter() < deadline:
        seq = (seq + 1) & 0xFFFFFFFF

        try:
            result, elapsed = ping(dev, seq, args.timeout)
        except (OSError, ValueError) as e:
            # The device vanished mid-transfer, which is itself a hard failure
            drops += 1
            print(f"\n[{time.strftime('%H:%M:%S')}] Link dropped: {e}")

            try:
                dev.close()
            except Exception:
                pass

            dev = None
            while dev is None and time.perf_counter() < deadline:
                time.sleep(0.25)
                path = find_device(args.vid, args.pid)
                if path is not None:
                    try:
                        dev = open_device(path)
                    except OSError:
                        dev = None

            if dev is None:
                break

            print(f"[{time.strftime('%H:%M:%S')}] Reconnected\n")
            continue

        counts[result] += 1
        if result == "ok":
            latencies.append(elapsed)
        else:
            print(f"\n[{time.strftime('%H:%M:%S')}] Packet {seq}: {result}")

        total = sum(counts.values())
        if total % 20 == 0:
            avg = statistics.fmean(latencies) if latencies else 0
            sys.stdout.write(f"\rsent {total}  ok {counts['ok']}  avg {avg:6.2f} ms")
            sys.stdout.flush()

        if args.interval > 0:
            time.sleep(args.interval / 1000)
except KeyboardInterrupt:
    pass

total = sum(counts.values())
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
    ordered = sorted(latencies)
    print()
    print(f"Latency min       {ordered[0]:.2f} ms")
    print(f"Latency median    {statistics.median(ordered):.2f} ms")
    print(f"Latency p99       {ordered[min(len(ordered) - 1, int(len(ordered) * 0.99))]:.2f} ms")
    print(f"Latency max       {ordered[-1]:.2f} ms")

if failed == 0 and drops == 0:
    print("\nLink looks healthy.")
else:
    print("\nLink errors detected.")
