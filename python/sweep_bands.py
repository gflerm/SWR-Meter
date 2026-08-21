"""Sweep each HF amateur band (160m-10m) with the AA-30.ZERO and record results.

The Uno R4 bridge (AA30_Bridge.ino) validates each frx measurement and prints it
on the PC console as:  F=<MHz>MHz R=<ohms> X=<ohms> SWR=<swr>
This script drives the bridge, parses those validated lines, and writes result.md.
"""
import serial, time, re, statistics, json, sys

# Windows console is cp1252; force UTF-8 so Ω/unicode prints don't crash.
try:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
except Exception:
    pass

PORT = "COM8"
BAUD = 9600
POINTS = 100             # frx99 -> n+1 = 100 sample points per band
MAX_HW_POINTS = 700      # analyzer max ~700 points (frx700 OK, frx800 stalls)

# IARU Region 1 amateur HF band plan (band name, low edge Hz, high edge Hz)
BANDS = [
    ("160m",  1_810_000,  2_000_000),
    ("80m",   3_500_000,  3_800_000),
    ("60m",   5_351_500,  5_366_500),
    ("40m",   7_000_000,  7_200_000),
    ("30m",  10_100_000, 10_150_000),
    ("20m",  14_000_000, 14_350_000),
    ("17m",  18_068_000, 18_168_000),
    ("15m",  21_000_000, 21_450_000),
    ("12m",  24_890_000, 24_990_000),
    ("10m",  28_000_000, 29_700_000),
]

LINE_RE = re.compile(r"F=([0-9.]+)MHz R=([0-9.]+) X=([-0-9.]+) SWR=([0-9.]+)")

def drain_until_idle(ser, wait=0.5):
    time.sleep(wait)
    out = b""
    while ser.in_waiting:
        out += ser.read(4096)
    return out.decode(errors="replace")

def send_and_wait(ser, cmd, delay=0.6):
    ser.reset_input_buffer()
    ser.write(cmd.encode())
    ser.flush()
    return drain_until_idle(ser, delay)

def do_sweep(ser, name, low, high):
    center = (low + high) // 2
    width = high - low
    send_and_wait(ser, "fq%d\r\n" % center)
    send_and_wait(ser, "sw%d\r\n" % width)
    ser.reset_input_buffer()
    ser.write(b"frx%d\r\n" % (POINTS - 1))
    ser.flush()

    data = []
    deadline = time.time() + 30.0
    while time.time() < deadline and len(data) < POINTS:
        time.sleep(0.4)
        chunk = ser.read(8192).decode(errors="replace")
        for m in LINE_RE.finditer(chunk):
            data.append((float(m.group(1)), float(m.group(2)),
                         float(m.group(3)), float(m.group(4))))
        if len(data) >= POINTS:
            break
    data = data[:POINTS]
    # safety: refetch if short (occasionally a chunk straddles the split)
    tries = 0
    while len(data) < POINTS and tries < 8:
        tries += 1
        time.sleep(0.5)
        chunk = ser.read(8192).decode(errors="replace")
        for m in LINE_RE.finditer(chunk):
            data.append((float(m.group(1)), float(m.group(2)),
                         float(m.group(3)), float(m.group(4))))
        data = data[:POINTS]
    return data

def write_json(results):
    """Save raw per-band data so the graph script can read it without re-sweeping."""
    payload = {}
    for name, low, high in BANDS:
        data = results.get(name, [])
        payload[name] = {
            "low": low, "high": high,
            "points": [{"f": d[0], "r": d[1], "x": d[2], "swr": d[3]} for d in data],
        }
    with open("result_data.json", "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=2)
    print("Wrote result_data.json")

def main():
    ser = serial.Serial(PORT, BAUD, timeout=1.0)
    time.sleep(2.0)
    send_and_wait(ser, "ver\r\n")

    results = {}
    for name, low, high in BANDS:
        data = do_sweep(ser, name, low, high)
        results[name] = data
        print(f"{name}: {len(data)} points   " +
              (f"R~{statistics.mean(d[1] for d in data):.1f} " if data else ""))

    ser.close()
    write_markdown(results)
    write_json(results)

def fmt(x):
    return "%.3f" % x

def write_markdown(results):
    lines = []
    lines.append("# AA-30.ZERO Band Sweep Results")
    lines.append("")
    lines.append("50 Ω reference load on the RF connector.")
    lines.append(f"Each band sampled at {POINTS} points via the R4 bridge (hardware Serial1 @ 38400).")
    lines.append("")
    lines.append("| Band | Freq (MHz) | R (Ω) | X (Ω) | SWR |")
    lines.append("|------|-----------|-------|-------|-----|")
    for name, low, high in BANDS:
        data = results.get(name, [])
        for (f, r, x, swr) in data:
            lines.append(f"| {name} | {f:.3f} | {r:.1f} | {x:.1f} | {swr:.2f} |")
        # add a per-band summary row
        if data:
            rs = [d[1] for d in data]
            xs = [d[2] for d in data]
            sws = [d[3] for d in data]
            lines.append(f"| **{name} avg** | **--** | **{statistics.mean(rs):.1f}** | "
                         f"**{statistics.mean(xs):.1f}** | **{statistics.mean(sws):.2f}** |")
    lines.append("")
    lines.append("_Measured with a 50 Ω dummy load: R should read ≈50 Ω, X ≈ 0 Ω, SWR ≈ 1.0._")
    lines.append("")
    text = "\n".join(lines)
    with open("result.md", "w", encoding="utf-8") as fh:
        fh.write(text)
    print("Wrote result.md")

if __name__ == "__main__":
    main()
