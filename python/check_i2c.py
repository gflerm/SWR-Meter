#!/usr/bin/env python3
"""Continuous I2C + battery checker for the SWR meter.

Repeatedly sends !I2C:SCAN and !GET:STATE to the board and prints the
results, so you can watch the bus come alive as you fix the wiring.

When the GT911 touch (0x5D) and MAX17043 gauge (0x36) are both on the bus
and powered, you should see:
    @I2C:0x5D 0x36 (count=2)
    @BATT:93,4120

Usage:
    python check_i2c.py [--port COM8] [--interval 2]
"""
import argparse
import sys
import time

import serial


def read_lines(ser, dur=1.4):
    buf = b""
    end = time.time() + dur
    out = []
    while time.time() < end:
        i = buf.find(b"\n")
        if i >= 0:
            out.append(buf[:i].decode("utf-8", "replace"))
            buf = buf[i + 1:]
            continue
        if ser.in_waiting:
            buf += ser.read(ser.in_waiting)
        else:
            time.sleep(0.01)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--interval", type=float, default=2.0)
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, 115200, timeout=0.1)
    except Exception as exc:
        print("could not open %s: %s" % (args.port, exc))
        return 2
    time.sleep(1.0)
    ser.reset_input_buffer()

    print("Watching I2C bus on %s. Ctrl-C to stop.\n" % args.port)
    try:
        while True:
            ser.write(b"!I2C:SCAN\n")
            time.sleep(0.4)
            lines = read_lines(ser)
            i2c = next((l for l in lines if "@I2C" in l), "(no reply)")
            ser.reset_input_buffer()
            ser.write(b"!GET:STATE\n")
            time.sleep(0.4)
            lines = read_lines(ser)
            batt = next((l for l in lines if "@BATT" in l), "?")
            print("%s   |   %s" % (i2c, batt), flush=True)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
