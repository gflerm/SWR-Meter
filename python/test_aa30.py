"""Test serial communication with the RigExpert AA-30 Zero through the Uno R4 bridge.

The bridge (AA30_Bridge.ino) forwards bytes between the PC USB-CDC port (Serial @ 115200)
and the AA-30 (hardware Serial1 @ 38400). Sending a command from this script is
translated to the AA-30 and its reply is forwarded back here.

Usage:
    python test_aa30.py [PORT]   e.g. python test_aa30.py COMxx
"""
import sys
import time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COMxx"
BAUD = 115200  # must match PC_BAUD in AA30_Bridge.ino

print(f"Opening {PORT} @ {BAUD} ...", flush=True)
ser = serial.Serial(PORT, BAUD, timeout=1.0)
# Opening the native-USB CDC port resets the Uno R4; wait for the boot banner.
time.sleep(2.5)
try:
    boot = ser.read(2048).decode(errors="replace")
    print(f"BOOT <{boot!r}>", flush=True)

    def send(cmd):
        ser.reset_input_buffer()
        print(f">>> {cmd!r}", flush=True)
        ser.write(cmd.encode())
        ser.flush()
        time.sleep(0.8)
        data = ser.read(4096).decode(errors="replace")
        print(f"<<< {data!r}", flush=True)
        return data

    print("=== AA-30 Zero comms test over Uno R4 bridge ===", flush=True)
    send("ver\n")
    send("ver\r\n")
    send("fq14000000\n")
    send("sw500000\n")
finally:
    ser.close()  # always release the port, even on Ctrl+C
    print("=== done (serial closed) ===", flush=True)
