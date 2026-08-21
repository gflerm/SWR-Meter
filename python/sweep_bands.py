"""Sweep each HF amateur band (Region 1, 160m-10m) with the AA-30.ZERO and record results.

The R4 bridge (AA30_Bridge.ino) validates each frx measurement and prints it on the
PC console as:  F=<MHz>MHz R=<ohms> X=<ohms> SWR=<swr>
This script drives the bridge, reassembles its lines across read boundaries to avoid
losing split points, confirms each command's OK, verifies the point count, and writes
result.md + result_data.json next to this script (i.e. the project root).

Usage:  python sweep_bands.py [PORT]     PORT defaults to COM8
"""
import os, re, sys, time, statistics, json

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # project root
PORT = sys.argv[1] if len(sys.argv) > 1 else "COM8"
BAUD = 115200
POINTS = 100            # frx99 -> n+1 = 100 samples per band
MAX_HW_POINTS = 700     # analyzer max ~700 points (frx700 OK, frx800 stalls)

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

LINE_RE = re.compile(r"F=([0-9.]+)MHz R=([0-9.]+) X=(-?[0-9.]+) SWR=([0-9.]+)")


class Stream:
    """Buffered line reader that reassembles lines split across serial reads."""

    def __init__(self, ser):
        self.ser = ser
        self.buf = b""

    def reset(self):
        self.ser.reset_input_buffer()
        self.buf = b""

    def readline(self, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            idx = self.buf.find(b"\n")
            if idx >= 0:
                line = self.buf[:idx]
                self.buf = self.buf[idx + 1:]
                return line.decode(errors="replace").strip()
            if self.ser.in_waiting:
                self.buf += self.ser.read(self.ser.in_waiting)
            else:
                time.sleep(0.01)
        return None

    def wait_ok(self, timeout=8.0):
        """Reads lines until one containing OK is seen; raises on timeout."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.readline(1.0)
            if line is None:
                continue
            if "OK" in line:
                return True
        raise TimeoutError("no OK from analyzer")


def send_ok(st, text):
    st.reset()
    st.ser.write(text.encode())
    st.ser.flush()
    st.wait_ok()


def expect_response(st, timeout=4.0):
    """Reads one non-empty line (e.g. the ver version string); None on timeout."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = st.readline(1.0)
        if line:
            return line
    return None


def sweep_band(st, name, low, high):
    center = (low + high) // 2
    span = high - low
    send_ok(st, "fq%d\r\n" % center)
    send_ok(st, "sw%d\r\n" % span)

    st.reset()
    st.ser.write(b"frx%d\r\n" % (POINTS - 1))
    st.ser.flush()

    pts = []
    deadline = time.time() + 30.0
    while time.time() < deadline and len(pts) < POINTS:
        line = st.readline(5.0)
        if line is None:
            break
        if "OK" in line:
            break
        m = LINE_RE.search(line)
        if m:
            pts.append((float(m.group(1)), float(m.group(2)),
                        float(m.group(3)), float(m.group(4))))

    if len(pts) != POINTS:
        raise RuntimeError(f"{name}: expected {POINTS} points, got {len(pts)}")
    return pts


def write_markdown(results):
    lines = [
        "# AA-30.ZERO Band Sweep Results",
        "",
        "50 \u03a9 reference load on the RF connector.",
        f"Each band sampled at {POINTS} points via the R4 bridge (hardware Serial1 @ 38400).",
        "IARU Region 1 band plan.",
        "",
        "| Band | Freq (MHz) | R (\u03a9) | X (\u03a9) | SWR |",
        "|------|-----------|-------|-------|-----|",
    ]
    for name, low, high in BANDS:
        for (f, r, x, swr) in results.get(name, []):
            lines.append(f"| {name} | {f:.3f} | {r:.1f} | {x:.1f} | {swr:.2f} |")
        rs = [d[1] for d in results.get(name, [])] or [0]
        xs = [d[2] for d in results.get(name, [])] or [0]
        sws = [d[3] for d in results.get(name, [])] or [0]
        lines.append(f"| **{name} avg** | **--** | **{statistics.mean(rs):.1f}** | "
                     f"**{statistics.mean(xs):.1f}** | **{statistics.mean(sws):.2f}** |")
    lines.append("")
    lines.append("_Measured with a 50 \u03a9 dummy load: R \u2248 50 \u03a9, X \u2248 0 \u03a9, SWR \u2248 1.0._")
    with open(os.path.join(BASE, "result.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


def write_json(results):
    payload = {
        name: {
            "low": low, "high": high,
            "points": [{"f": d[0], "r": d[1], "x": d[2], "swr": d[3]} for d in results[name]],
        }
        for name, low, high in BANDS if results.get(name)
    }
    with open(os.path.join(BASE, "result_data.json"), "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=2)


def main():
    import serial
    ser = serial.Serial(PORT, BAUD, timeout=1.0)
    st = Stream(ser)
    results = {}
    try:
        time.sleep(2.0)
        st.reset()
        st.ser.write(b"ver\r\n")
        st.ser.flush()
        ver = expect_response(st, 8.0)
        if ver:
            print(f"Analyzer: {ver}")
        else:
            print("WARNING: no response to ver")

        for name, low, high in BANDS:
            results[name] = sweep_band(st, name, low, high)
            rs = [d[1] for d in results[name]]
            print(f"{name}: {len(results[name])} points   R~{statistics.mean(rs):.1f}")
    finally:
        ser.close()

    write_markdown(results)
    write_json(results)
    print(f"Wrote {os.path.join(BASE, 'result.md')} and result_data.json")


if __name__ == "__main__":
    main()
