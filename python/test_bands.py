#!/usr/bin/env python3
"""Sweep all HF bands through the Uno R4 firmware and report R/X/SWR.

Connect to the Uno R4's USB-CDC port (COM8), drive it via the !BTN: soft-button
commands and !GET:STATE telemetry, and report the measured values per band.

The firmware runs the real START->SCANNING->DISPLAYING state machine and emits
@POINT:freq,R,X,swr for every valid measurement point. This script collects
those and prints a per-band summary (min SWR + R/X at the minimum).

Usage:  python test_bands.py [--port COM8] [--baud 115200]
"""
import argparse
import re
import sys
import time
import serial

REFS = {  # band from the firmware BANDS table
    "160m": None, "80m": None, "60m": None, "40m": None, "30m": None,
    "20m": None, "17m": None, "15m": None, "12m": None, "10m": None,
}
BAND_ORDER = ["160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m"]


def drain(ser, dur=0.2):
    ser.timeout = 0.2
    buf = b""
    end = time.time() + dur
    while time.time() < end:
        try:
            chunk = ser.read(4096)
        except Exception:
            break
        if not chunk:
            break
        buf += chunk
    return buf.decode("utf-8", "replace")


class Firmware:
    def __init__(self, port, baud):
        self.port = port
        self.ser = serial.Serial(port, baud, timeout=0.2, write_timeout=0.5)
        time.sleep(0.5)
        self.ser.reset_input_buffer()

    def cmd(self, line, settle=0.3):
        self.ser.write((line + "\n").encode())
        time.sleep(settle)
        return drain(self.ser, 0.2)

    def state(self):
        return self.cmd("!GET:STATE", 0.25)

    def band(self):
        m = re.search(r"@BAND:(\S+)", self.state())
        return m.group(1) if m else None

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


def do_scan(fw, timeout=12.0):
    """Trigger a scan and collect @POINT: lines until SCANNING ends."""
    fw.ser.reset_input_buffer()
    fw.cmd("!BTN:START", 0.2)  # leave any state, begin scan

    points = []
    start = time.time()
    fw.ser.timeout = 0.2
    buf = b""
    saw_scanning = False
    while time.time() - start < timeout:
        chunk = fw.ser.read(4096)
        if not chunk:
            continue
        buf += chunk
        text = buf.decode("utf-8", "replace")
        for m in re.finditer(r"@POINT:([^\\r\\n]+)", text):
            p = m.group(1).split(",")
            if len(p) == 4:
                try:
                    points.append((float(p[0]), float(p[1]), float(p[2]), float(p[3])))
                except ValueError:
                    pass
        if "@STATE:SCANNING" in text:
            saw_scanning = True
        # scan done when we see a DISPLAYING or a fresh band/state after points
        if points and ("@STATE:DISPLAYING" in text or "@STATE:IDLE" in text):
            break
    return points


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=12.0)
    args = ap.parse_args()

    try:
        fw = Firmware(args.port, args.baud)
    except Exception as exc:
        print(f"Could not open {args.port}: {exc}")
        sys.exit(1)

    print(f"Connected to {args.port}. Scanning each band (this tests the real "
          f"AA-30 through the firmware)...")
    print()

    # Normalise to a known starting band: cycle to 160m first.
    for _ in range(20):
        if fw.band() == "160m":
            break
        fw.cmd("!BTN:BAND", 0.2)
        time.sleep(0.1)

    results = []
    for name in BAND_ORDER:
        # ensure on this band
        for _ in range(20):
            if fw.band() == name:
                break
            fw.cmd("!BTN:BAND", 0.2)
            time.sleep(0.1)
        print(f"--- {name} ---")
        pts = do_scan(fw, timeout=args.timeout)
        if pts:
            best = min(pts, key=lambda p: p[3])
            print(f"  points={len(pts)}  minSWR={best[3]:.3f} @ {best[0]:.3f} MHz  "
                  f"R={best[1]:.1f}  X={best[2]:.1f}")
            results.append((name, len(pts), best[0], best[1], best[2], best[3]))
        else:
            print("  NO VALID POINTS (AA-30 returned no data)")
            results.append((name, 0, None, None, None, None))

    fw.close()

    print()
    print("=" * 66)
    print(f"{'Band':<6}{'Pts':>5}{'R (ohm)':>10}{'X (ohm)':>10}{'MinSWR':>9}")
    print("-" * 66)
    for name, n, f, r, x, swr in results:
        if n:
            print(f"{name:<6}{n:>5}{r:>10.1f}{x:>10.1f}{swr:>9.3f}")
        else:
            print(f"{name:<6}{n:>5}{'--':>10}{'--':>10}{'--':>9}")
    print("=" * 66)


if __name__ == "__main__":
    main()
