#!/usr/bin/env python3
"""Guided G5 calibration walkthrough for the AA-30.ZERO SWR meter.

Drives the firmware's calibration wizard over USB-CDC and tells you exactly
when to swap the calibration plug on the AA-30's RF connector:

  Phase 1/3  -> connect a 50 ohm load            (run)
  Phase 2/3  -> swap to a SHORT                   (run)
  Phase 3/3  -> swap to an OPEN                   (run)

Press Enter at each prompt after changing/confirming the plug. The script keys
off the firmware's @CALPHASE / @STATE / @CALPROG telemetry so timing is driven
by the device, not a blind timer.

It writes a structured result log:
  tests/calibration_result.json   (machine-readable)
  tests/calibration_result.md     (human-readable summary)

Usage:
  python calibrate_guide.py [--port COM8] [--out tests/calibration_result]
"""
import json
import os
import re
import sys
import time
import serial

RT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CALPHASE_RE = re.compile(r"@CALPHASE:(\d+)/\d+")
STATE_RE = re.compile(r"@STATE:(\w+)")
POINT_RE = re.compile(r"@POINT:([^\r\n]+)")

BANDS = ["160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m"]
PHASES = ["50 ohm", "SHORT", "OPEN"]


class Firmware:
    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baud, timeout=0.05, write_timeout=0.5)
        self.buf = b""
        time.sleep(0.5)
        self.ser.reset_input_buffer()

    def write(self, t):
        self.ser.write((t + "\n").encode())

    def read_lines(self, dur=0.5):
        end = time.time() + dur
        out = []
        while time.time() < end:
            idx = self.buf.find(b"\n")
            if idx >= 0:
                line = self.buf[:idx].decode("utf-8", "replace").strip()
                self.buf = self.buf[idx + 1:]
                if line:
                    out.append(line)
                continue
            if self.ser.in_waiting:
                self.buf += self.ser.read(self.ser.in_waiting)
            else:
                time.sleep(0.005)
        return out

    def state(self):
        self.buf = b""
        self.write("!GET:STATE")
        return "\n".join(self.read_lines(1.0))

    def wait_for(self, pattern, timeout=120.0, verbose=True):
        """Return (matched_line, collected_lines) once a line matches.

        If verbose, streams live progress to stdout: band changes and each
        band's PASS/FAIL (from @CALPROG / @CALBAND telemetry).
        """
        rx = re.compile(pattern)
        end = time.time() + timeout
        got = []
        last_band = 0
        self.buf = b""
        while time.time() < end:
            idx = self.buf.find(b"\n")
            if idx >= 0:
                line = self.buf[:idx].decode("utf-8", "replace").strip()
                self.buf = self.buf[idx + 1:]
                got.append(line)
                if verbose:
                    m = re.search(r"@CALPROG:band=(\d+)/\d+,pt=(\d+)/\d+", line)
                    if m:
                        bnum = int(m.group(1))
                        if bnum != last_band:
                            print("    Band %d/10 ..." % bnum)
                            last_band = bnum
                        continue
                    vb = re.search(r"@CALBAND:band=(\d+),pass=(\d)", line)
                    if vb:
                        print("    Band %s: %s" % (vb.group(1),
                                                   "PASS" if vb.group(2) == "1" else "FAIL"))
                        continue
                if rx.search(line):
                    return m, got
                continue
            if self.ser.in_waiting:
                self.buf += self.ser.read(self.ser.in_waiting)
            else:
                time.sleep(0.01)
        return None, got

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


def capture_phase(fw, phase_name, advance_pattern, timeout=120.0):
    """Trigger the phase sweep and collect point samples until the phase
    advances (advance_pattern) or the wizard finishes."""
    fw.write("!BTN:START")
    print("Sweeping all bands vs %s... (this runs automatically)" % phase_name)
    points = []
    got = []
    m, lines = fw.wait_for(advance_pattern, timeout)
    # Re-scan the already-collected lines for point telemetry and final state.
    for ln in lines:
        pm = POINT_RE.search(ln)
        if pm:
            p = pm.group(1).split(",")
            if len(p) == 4:
                try:
                    points.append([float(x) for x in p])
                except ValueError:
                    pass
        if "@STATE:CAL_DONE" in ln:
            got.append("CAL_DONE")
        if "@CALRESULT:" in ln:
            got.append(ln)
    return m, points, got


def main():
    args = [a for a in sys.argv[1:]]
    port = "COM8"
    out_base = os.path.join(RT, "tests", "calibration_result")
    if "--port" in args:
        port = args[args.index("--port") + 1]
    if "--out" in args:
        out_base = args[args.index("--out") + 1]

    try:
        fw = Firmware(port, 115200)
    except Exception as exc:
        print("Could not open %s: %s" % (port, exc))
        return 2

    print("=== G5 CALIBRATION WIZARD ===")
    print("Board: %s   (leave the AA-30 RF port plugged into the R4)" % port)
    print()

    # Ensure IDLE, then enter calibration.
    fw.write("!BTN:MODE"); time.sleep(0.6)
    fw.write("!BTN:CAL"); time.sleep(0.8)
    s = fw.state()
    if "@STATE:CALIBRATE" not in s:
        print("ERROR: did not enter CALIBRATE. State: %s" % s.strip()[:80])
        fw.close()
        return 1
    print("Entered calibration (phase 1/3).")
    print()

    results = {"started": time.strftime("%Y-%m-%d %H:%M:%S"),
               "port": port, "phases": {}, "final_state": None, "verdict": None}
    last_got = []

    for idx, phase in enumerate(PHASES, start=1):
        nxt = idx + 1
        if nxt <= 3:
            advance = r"@CALPHASE:%d/3" % nxt
        else:
            advance = r"@STATE:CAL_DONE"
        prompt = ("[PHASE %d/3 - %s] Connect %s on the RF connector, "
                  "then press Enter to start..." % (idx, phase.upper(), phase))
        input(prompt)
        m, points, got = capture_phase(fw, phase, advance)
        last_got = got
        # Sample stats
        stats = None
        if points:
            rs = [p[1] for p in points]
            xs = [p[2] for p in points]
            sws = [p[3] for p in points]
            stats = {
                "points": len(points),
                "r_min": round(min(rs), 2), "r_max": round(max(rs), 2),
                "r_avg": round(sum(rs) / len(rs), 2),
                "x_avg": round(sum(xs) / len(xs), 2),
                "swr_min": round(min(sws), 3), "swr_max": round(max(sws), 3),
                "sample": points[0] if points else None,
            }
        # Did we advance (phase transition or wizard end)?
        advanced = bool(m)
        cal_done = "CAL_DONE" in got
        results["phases"][phase] = {
            "advanced": advanced, "cal_done_in_phase": cal_done, "stats": stats,
        }
        print("  %s: points=%s advanced=%s cal_done=%s" % (
            phase, stats["points"] if stats else 0, advanced, cal_done))
        print()

    # Final state / verdict.
    s = fw.state()
    mstate = STATE_RE.search(s)
    final_state = mstate.group(1) if mstate else s.strip()[:60]
    results["final_state"] = final_state
    verdict = "UNKNOWN"
    mres = re.search(r"@CALRESULT:(PASS|FAIL)", "\n".join(last_got + [s]))
    if mres:
        verdict = mres.group(1)
    elif "CALIBRATION PASSED" in "\n".join(last_got + [s]):
        verdict = "PASS"
    elif "CALIBRATION FAILED" in "\n".join(last_got + [s]):
        verdict = "FAIL"
    results["verdict"] = verdict
    print("=== RESULT ===")
    print("final_state=%s verdict=%s" % (final_state, verdict))
    print("\n".join(last_got + [s]).replace("@", "\n@")[:600])
    fw.close()

    # Write logs.
    os.makedirs(os.path.dirname(out_base), exist_ok=True)
    with open(out_base + ".json", "w", encoding="utf-8") as fh:
        json.dump(results, fh, indent=2)
    lines = ["# Calibration result (G5)", "",
             "- Started: `%s`" % results["started"],
             "- Port: `%s`" % results["port"],
             "- Final state: `%s`" % final_state,
             "- Verdict: **%s**" % verdict, "",
             "| Phase | Advanced | Points | R avg | X avg | SWR min |",
             "|-------|----------|--------|-------|-------|---------|"]
    for name, d in results["phases"].items():
        st = d["stats"]
        if st:
            lines.append("| %s | %s | %d | %.1f | %.1f | %.3f |" % (
                name, d["advanced"], st["points"], st["r_avg"], st["x_avg"],
                st["swr_min"]))
        else:
            lines.append("| %s | %s | 0 | - | - | - |" % (name, d["advanced"]))
    lines.append("")
    with open(out_base + ".md", "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print("Wrote %s.json and %s.md" % (out_base, out_base))
    return 0


if __name__ == "__main__":
    sys.exit(main())
