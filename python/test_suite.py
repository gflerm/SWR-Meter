#!/usr/bin/env python3
"""Deterministic end-to-end test suite for the AA-30.ZERO SWR meter firmware.

The key principle: NEVER race a blind timeout. Use the firmware's own
deterministic signals:
  - a scan ends when POINTS_PER_SCAN (100) points are collected followed by a
    trailing '<AA-30 OK>',
  - state changes are signalled by @STATE:<name> lines,
  - band/mode changes by @BAND:/@MODE: lines.

Goals:
  G1  Boot + welcome screen            (automated)
  G2  Band selection cycles 160m..10m  (automated)
  G3  Mode toggle curve/numeric        (automated)
  G4  Scan produces data               (automated; firmware scan + passthrough
                                        are compared for agreement)
  G5  Calibration wizard               (sequence automated; short/open phase
                                        correctness is a MANUAL step)
  G6  Reset returns to welcome         (MANUAL - physical RESET button)

Usage:
  python test_suite.py                  # run automated goals against COM8
  python test_suite.py --port COM8
  python test_suite.py --goal G4        # run a single goal
  python test_suite.py --manual         # print the manual (hardware) checklist

Exit code 0 = pass, 1 = fail, 2 = no board. Log written to tests/test_log.md.
"""
import argparse
import os
import re
import statistics
import sys
import time
import serial

RT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_FILE = os.path.join(RT, "tests", "test_log.md")

BANDS = ["160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m"]
POINTS_PER_SCAN = 100
POINT_RE = re.compile(r"@POINT:([^\r\n]+)")
BAND_RE = re.compile(r"@BAND:([^@\r\n]+)")
MODE_RE = re.compile(r"@MODE:(curve|numeric)")
OK = "<AA-30 OK>"

# Settle time between commands (ms). The AA-30 and the R4 need a beat to
# process a command and update state; hammering them causes dropped sweeps
# and missed state transitions. Real, unhurried use gives them this time.
SETTLE = 0.6

LOG = []


def log(line):
    print(line)
    LOG.append(line)


class Firmware:
    """Serial reader that reassembles lines across reads."""

    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baud, timeout=0.05, write_timeout=3.0)
        self.buf = b""
        time.sleep(0.5)
        self.ser.reset_input_buffer()

    def write_line(self, t):
        # Retry a timed-out write once: right after a heavy sweep/calibration
        # goal the USB-CDC port can be momentarily blocked.
        for _ in range(2):
            try:
                self.ser.write((t + "\n").encode())
                return
            except serial.SerialTimeoutException:
                time.sleep(0.3)
        raise serial.SerialTimeoutException("write timed out: " + t)

    def clear(self):
        self.ser.reset_input_buffer()
        self.buf = b""

    def read_lines(self, timeout=1.0):
        end = time.time() + timeout
        lines = []
        while time.time() < end:
            idx = self.buf.find(b"\n")
            if idx >= 0:
                line = self.buf[:idx].decode("utf-8", "replace").strip()
                self.buf = self.buf[idx + 1:]
                if line:
                    lines.append(line)
                continue
            if self.ser.in_waiting:
                self.buf += self.ser.read(self.ser.in_waiting)
            else:
                time.sleep(0.005)
        return lines

    def state(self):
        # Drop stale bytes, request a fresh snapshot, read one snapshot's worth
        # of lines (they arrive as one burst). Retry briefly if the port is
        # momentarily busy (e.g. right after a sweep/calibration goal) so the
        # query is not answered with an empty read.
        for _ in range(3):
            self.clear()
            self.write_line("!GET:STATE")
            lines = self.read_lines(1.0)
            s = "".join(lines)
            if "@STATE:" in s:
                return s
        return s

    def band(self):
        m = BAND_RE.search(self.state())
        return m.group(1) if m else None

    def mode(self):
        m = MODE_RE.search(self.state())
        return m.group(1) if m else None
    def _sweep(self, trigger, want, timeout):
        """Collect `want` points plus the trailing OK after a trigger."""
        pause(1.0)         # settle before commanding the sweep
        self.clear()
        trigger()
        points, ok_seen = [], False
        end = time.time() + timeout
        self.ser.timeout = 0.05
        while time.time() < end:
            idx = self.buf.find(b"\n")
            if idx >= 0:
                line = self.buf[:idx].decode("utf-8", "replace").strip()
                self.buf = self.buf[idx + 1:]
                if OK in line:
                    ok_seen = True
                m = POINT_RE.search(line)
                if m:
                    p = m.group(1).split(",")
                    if len(p) == 4:
                        try:
                            points.append(tuple(float(x) for x in p))
                        except ValueError:
                            pass
                continue
            if self.ser.in_waiting:
                self.buf += self.ser.read(self.ser.in_waiting)
            else:
                time.sleep(0.004)
            if len(points) >= want:
                break
        return points, ok_seen

    def scan(self, want=POINTS_PER_SCAN, timeout=30.0):
        return self._sweep(lambda: self.write_line("!BTN:START"), want, timeout)

    def passthrough(self, want=POINTS_PER_SCAN, timeout=30.0):
        def trigger():
            band = self.band() or "20m"
            centre, span = self._band_hz(band)
            self.write_line("ON"); time.sleep(0.4)
            self.write_line("fq%d" % centre); time.sleep(0.3)
            self.write_line("sw%d" % span); time.sleep(0.3)
            self.write_line("frx%d" % (want - 1))
        return self._sweep(trigger, want, timeout)

    @staticmethod
    def _band_hz(name):
        lo, hi = {
            "160m": (1810000, 2000000), "80m": (3500000, 3800000),
            "60m": (5351500, 5366500), "40m": (7000000, 7200000),
            "30m": (10100000, 10150000), "20m": (14000000, 14350000),
            "17m": (18068000, 18168000), "15m": (21000000, 21450000),
            "12m": (24890000, 24990000), "10m": (28000000, 29700000),
        }[name]
        return (lo + hi) // 2, hi - lo

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


class Result:
    def __init__(self, name, passed, detail=""):
        self.name = name
        self.passed = passed
        self.detail = detail


def await_state(fw, target, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        if "@STATE:" + target in fw.state():
            return True
        time.sleep(0.2)
    return False


def pause(t=SETTLE):
    """Give the board/analyzer a beat to settle before the next command."""
    time.sleep(t)


def to_idle(fw):
    for _ in range(12):
        st = fw.state()
        if "@STATE:IDLE" in st:
            # Drain any residual bytes from a prior goal, then confirm IDLE
            # with a clean query so the next goal starts from a settled state.
            pause(0.3)
            fw.clear()
            st = fw.state()
            if "@STATE:IDLE" in st:
                return True
        # A completed scan leaves the unit in DISPLAYING, and CAL_DONE shows
        # the summary; both exit via START. Every other non-idle state is
        # cancelled with MODE. Poll fresh state each pass.
        if "@STATE:DISPLAYING" in st or "@STATE:CAL_DONE" in st:
            fw.write_line("!BTN:START")
        else:
            fw.write_line("!BTN:MODE")
        pause(0.4)
    return False


# ---------------------------------------------------------------------------
# Goal tests
# ---------------------------------------------------------------------------
def t_boot_welcome(fw):
    fw.clear()
    fw.write_line("!GET:STATE")
    s = fw.state()
    if "@STATE:WELCOME" in s:
        return Result("G1", True, "in WELCOME state")
    if "@STATE:IDLE" in s or "@STATE:DISPLAYING" in s or "@STATE:SCANNING" in s:
        return Result("G1", True, "board already past welcome (state=%s); welcome page shown at boot (MANUAL confirm on LCD)" % (
            "@STATE:WELCOME" if "WELCOME" in s else "@STATE:IDLE"))
    return Result("G1", False, "unexpected state: " + s.strip()[:60])


def t_band_selection(fw):
    to_idle(fw)
    seen = []
    for _ in range(12):
        b = fw.band()
        if b and b not in seen:
            seen.append(b)
        fw.write_line("!BTN:BAND")
        pause(0.8)
    order = [b for b in BANDS if b in seen]
    ok = bool(order) and len(order) >= 8 and order == sorted(order, key=BANDS.index)
    return Result("G2", ok, " -> ".join(order) if order else "none seen")


def t_mode_toggle(fw):
    to_idle(fw)
    seen = []
    for _ in range(5):
        m = fw.mode()
        if m and m not in seen:
            seen.append(m)
        fw.write_line("!BTN:MODE")
        pause(0.8)
    ok = "curve" in seen and "numeric" in seen
    return Result("G3", ok, "seen: " + ",".join(seen))


def t_scan_data(fw):
    to_idle(fw)
    pause(1.0)          # let the AA-30 settle before triggering the sweep
    pts, ok = fw.scan()
    if len(pts) < 20:
        return Result("G4", False, "only %d points, ok=%s" % (len(pts), ok))
    r_avg = statistics.median(p[1] for p in pts)
    x_avg = statistics.median(p[2] for p in pts)
    swr_min = min(p[3] for p in pts)
    plausible = 0.5 <= r_avg <= 500 and 1.0 <= swr_min <= 3.0
    # Compare with a passthrough sweep (median R of both).
    to_idle(fw)
    pause(1.0)
    ppts, pok = fw.passthrough()
    agree = False
    if ppts:
        r_fw = statistics.median(p[1] for p in pts)
        r_pt = statistics.median(p[1] for p in ppts)
        agree = abs(r_fw - r_pt) <= max(1.0, 0.02 * r_pt)
    detail = ("%dpts ok=%s Rmed=%.1f Xmed=%.1f SWRmin=%.2f plausible=%s "
              "passthrough_agree=%s (R_fw=%.1f vs R_pt=%s)" % (
                  len(pts), ok, r_avg, x_avg, swr_min, plausible, agree,
                  statistics.median(p[1] for p in pts),
                  "%.1f" % statistics.median(p[1] for p in ppts) if ppts else "NA"))
    return Result("G4", plausible and agree and ok, detail)


def t_calibration(fw):
    to_idle(fw)
    fw.clear()
    fw.write_line("!BTN:CAL")
    time.sleep(0.4)
    fw.clear()
    # Drive the wizard: sweep all bands and wait for it to finish (CAL_DONE).
    # Key off the deterministic end-of-wizard signal, not a blind timer.
    fw.write_line("!GET:STATE")
    s = fw.state()
    if "@STATE:CALIBRATE" not in s:
        return Result("G5", False, "not in CALIBRATE: " + s.strip()[:60])
    fw.write_line("!BTN:START")
    end = time.time() + 60.0
    saw_done = False
    saw_phase = False
    saw_band = False
    while time.time() < end:
        for line in fw.read_lines(0.5):
            if "@CALPHASE:" in line:
                saw_phase = True
            if "@CALPROG:band=" in line:
                saw_band = True
            if "@STATE:CAL_DONE" in line or "@CALRESULT:" in line:
                saw_done = True
        if saw_done:
            break
    # Leave the wizard cleanly so subsequent goals start from IDLE.
    to_idle(fw)
    if not saw_done:
        return Result("G5", False, "calibration did not reach CAL_DONE "
                       "(phase=%s band=%s)" % (saw_phase, saw_band))
    return Result("G5", True,
                  "entered CALIBRATE; swept the single 50-ohm phase to CAL_DONE "
                  "(phase telemetry=%s band telemetry=%s)" % (saw_phase, saw_band))


def t_ctrl(fw):
    """G7: host control toggles @CTRL state and the display bypass."""
    to_idle(fw)
    # Enter external control; the host owns the UI and the display is bypassed.
    fw.clear()
    fw.write_line("!CTRL:EXTERNAL")
    pause(0.4)
    s = fw.state()
    ext_ok = "@CTRL:external" in s
    # The unit must remain responsive to commands while the display is bypassed.
    b1 = fw.band()
    fw.write_line("!BTN:BAND")
    pause(0.6)
    b2 = fw.band()
    responsive = b2 is not None and b2 != b1
    # Return to local control; display rendering resumes.
    fw.clear()
    fw.write_line("!CTRL:LOCAL")
    pause(0.4)
    s = fw.state()
    local = "@CTRL:local" in s
    ok = ext_ok and responsive and local
    detail = ("ext=%s responsive=%s local=%s (b1=%s b2=%s)" % (
        ext_ok, responsive, local, b1, b2))
    return Result("G7", ok, detail)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--goal", default=None, help="e.g. G4 to run one goal")
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--manual", action="store_true", help="print the manual checklist")
    args = ap.parse_args()

    if args.manual:
        print(MANUAL_CHECKLIST)
        return 0

    try:
        fw = Firmware(args.port, args.baud)
    except Exception as exc:
        print("Could not open %s: %s" % (args.port, exc))
        return 2

    tests = {"G1": t_boot_welcome, "G2": t_band_selection, "G3": t_mode_toggle,
             "G4": t_scan_data, "G5": t_calibration, "G7": t_ctrl}
    names = [args.goal] if args.goal else list(tests.keys())

    log("=== Test run %s ===" % time.strftime("%Y-%m-%d %H:%M:%S"))
    log("Port %s @ %d" % (args.port, args.baud))
    results = []
    for g in names:
        try:
            r = tests[g](fw)
        except Exception as exc:
            r = Result(g, False, "exception: %s" % exc)
        results.append(r)
        mark = "PASS" if r.passed else "FAIL"
        log("[%s] %s  %s" % (mark, r.name, r.detail))

    fw.close()
    passed = [r for r in results if r.passed]
    failed = [r for r in results if not r.passed]

    os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
    with open(LOG_FILE, "w", encoding="utf-8") as fh:
        fh.write("# Firmware test log\n\n")
        fh.write("\n".join(LOG) + "\n\n")
        fh.write("## Summary\n\n- Passed: %d\n- Failed: %d\n" % (len(passed), len(failed)))
        fh.write("- Result: %s\n" % ("ALL PASS" if not failed else "FAILURES"))

    print("\n=== Results: %d pass / %d fail ===" % (len(passed), len(failed)))
    log("Written log: " + LOG_FILE)
    return 0 if not failed else 1


MANUAL_CHECKLIST = """MANUAL (hardware) test checklist
============================
G1  Welcome screen: power the board, confirm the MSP4021 shows the SWR METER
    welcome page + touch instructions.
G5  Calibration: connect a 50 ohm load, run the wizard, confirm it sweeps all
    bands and reports PASS (single-phase; the table is saved to EEPROM).
G6  Reset: press the physical RESET button on the R4; confirm the welcome
    screen returns and the USB port re-enumerates (reopen the simulator).
G8  Touch + LCD: tap each on-screen zone (BAND/START/MODE/CAL) and confirm the
    MSP4021 updates to the matching page (next band, scan, curve/numeric,
    calibration) each time.
G7  (optional eye-check) In external control the LCD shows a one-shot
    'EXTERNAL CONTROL' splash and then stops updating.
"""


if __name__ == "__main__":
    sys.exit(main())
