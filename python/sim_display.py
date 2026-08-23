#!/usr/bin/env python3
"""Simulator GUI for the AA-30.ZERO SWR meter firmware.

Emulates the ILI9341 display on a Windows PC and provides soft buttons that
inject button presses into the firmware over serial (USB CDC), so the unit can
be tested/verified without pressing physical buttons. Requires:

  - the firmware built from src/main.cpp (PC telemetry + !BTN: commands)
  - pyserial  (pip install pyserial)
  - Pillow    (pip install pillow)

Usage:
  python sim_display.py --port COM7
  python sim_display.py --port COM7 --baud 115200

Serial protocol (firmware -> PC):
  @STATE:<WELCOME|IDLE|CALIBRATE|CAL_DONE|SCANNING|DISPLAYING>
  @BAND:<name>
  @MODE:curve|numeric
  @POINT:<freqMHz>,<R>,<X>,<SWR>
  @CALPHASE:<n>/<total>
  @CALPROG:band=<b>/<N>,pt=<p>/<P>

Protocol (PC -> firmware):
  !BTN:START / !BTN:BAND / !BTN:MODE / !BTN:CAL
"""
import argparse
import os
import queue
import re
import threading
import time
import tkinter as tk
from tkinter import ttk

from PIL import Image, ImageTk
import serial

# ---------------------------------------------------------------------------
# Pixel-exact Adafruit 5x7 font (reused from the firmware's glcdfont.c)
# ---------------------------------------------------------------------------
BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_SRC = os.path.join(
    BASE, ".pio", "libdeps", "uno_r4_minima", "Adafruit GFX Library", "glcdfont.c"
)

W, H = 320, 240

BLACK = (0, 0, 0)
BLUE = (0, 0, 255)
GREEN = (0, 255, 0)
CYAN = (0, 255, 255)
RED = (255, 0, 0)
YELLOW = (255, 255, 0)
WHITE = (255, 255, 255)
ORANGE = (255, 165, 0)
LIGHTGREY = (198, 195, 198)
DARKGREY = (123, 125, 123)


def load_glcdfont():
    import re as _re
    src = open(FONT_SRC, encoding="utf-8").read()
    block = src[src.index("font[]"): src.index("};", src.index("font[]"))]
    hexes = _re.findall(r"0x([0-9A-Fa-f]{2})", block)
    assert len(hexes) == 256 * 5, f"expected 1280 bytes, got {len(hexes)}"
    glyphs = []
    for g in range(256):
        cols = [int(hexes[g * 5 + c], 16) for c in range(5)]
        glyph = [[(cols[c] >> row) & 1 for c in range(5)] for row in range(7)]
        glyphs.append(glyph)
    return glyphs


GLYPHS = load_glcdfont()


class TFT:
    """Renders into a PIL RGB image using the Adafruit 5x7 font (6x8 cells)."""

    def __init__(self):
        self.img = Image.new("RGB", (W, H), BLACK)
        self._px = self.img.load()
        self.text_color = WHITE
        self.bg_color = BLACK
        self.text_size = 1
        self.cursor = (0, 0)

    def fillScreen(self, color):
        self.img.paste(color, (0, 0, W, H))

    def fillRect(self, x, y, w, h, color):
        self.img.paste(color, box=(x, y, min(x + w, W), min(y + h, H)))

    def drawRect(self, x, y, w, h, color):
        for i in range(w):
            self._set(x + i, y, color)
            self._set(x + i, y + h - 1, color)
        for i in range(h):
            self._set(x, y + i, color)
            self._set(x + w - 1, y + i, color)

    def drawLine(self, x0, y0, x1, y1, color):
        from PIL import ImageDraw
        ImageDraw.Draw(self.img).line([x0, y0, x1, y1], fill=color)

    def _set(self, x, y, color):
        if 0 <= x < W and 0 <= y < H:
            self._px[x, y] = color

    def setTextColor(self, fg, bg=None):
        self.text_color = fg
        self.bg_color = bg if bg is not None else fg

    def setTextSize(self, s):
        self.text_size = s

    def setCursor(self, x, y):
        self.cursor = (x, y)

    def _draw_char(self, ch):
        n = self.text_size
        cx, cy = self.cursor
        glyph = GLYPHS[ord(ch) % 256]
        if self.bg_color != self.text_color:
            for row in range(7):
                for col in range(5):
                    x0 = cx + col * n
                    y0 = cy + row * n
                    for dy in range(n):
                        for dx in range(n):
                            self._set(x0 + dx, y0 + dy, self.bg_color)
        for row in range(7):
            for col in range(5):
                if glyph[row][col]:
                    x0 = cx + col * n
                    y0 = cy + row * n
                    for dy in range(n):
                        for dx in range(n):
                            self._set(x0 + dx, y0 + dy, self.text_color)
        self.cursor = (cx + 6 * n, cy)

    def _render(self, s):
        for ch in s:
            self._draw_char(ch)

    def print(self, *args):
        for a in args:
            self._render(str(a))

    def println(self, *args):
        for a in args:
            self._render(str(a) + "\n")
        x, y = self.cursor
        self.cursor = (x, y + 8 * self.text_size)


# ---------------------------------------------------------------------------
# Screen renderers (mirror src/main.cpp)
# ---------------------------------------------------------------------------
BANDS = {
    "160m": (1.810, 2.000), "80m": (3.500, 3.800), "60m": (5.3515, 5.3665),
    "40m": (7.000, 7.200), "30m": (10.100, 10.150), "20m": (14.000, 14.350),
    "17m": (18.068, 18.168), "15m": (21.000, 21.450), "12m": (24.890, 24.990),
    "10m": (28.000, 29.700),
}
CAL_PTS = 20
NUM_PHASES = 3


class ScreenModel:
    def __init__(self):
        self.state = "WELCOME"
        self.band = "20m"
        self.mode = "curve"
        self.points = []          # list of (freqMHz, swr)
        self.cal_phase = 1
        self.cal_phase_prompt = "Connect 50 ohm load"
        self.cal_band = 1
        self.cal_pt = 1
        self.cal_done_pass = True
        self.cal_done_fails = 0

    def set(self, key, val):
        if key == "STATE":
            self.state = val
        elif key == "BAND":
            self.band = val
        elif key == "MODE":
            self.mode = val
        elif key == "POINT":
            f, r, x, swr = val
            self.points.append((f, swr))
        elif key == "CALPHASE":
            n, total = val
            self.cal_phase = n
        elif key == "CALPROG":
            b, nb, p, np = val
            self.cal_band = b
            self.cal_pt = p


PROMPTS = {1: "Connect 50 ohm load", 2: "Connect SHORT", 3: "Connect OPEN"}


def render(sm):
    t = TFT()
    if sm.state == "WELCOME":
        render_welcome(t)
    elif sm.state == "IDLE":
        render_idle(t, sm)
    elif sm.state == "SCANNING":
        render_scanning(t, sm)
    elif sm.state == "CALIBRATE":
        if sm.cal_pt <= 1 and len(sm.points) == 0:
            render_cal_prompt(t, sm)
        else:
            render_cal_progress(t, sm)
    elif sm.state == "CAL_DONE":
        render_cal_done(t, sm)
    elif sm.state == "DISPLAYING":
        if sm.mode == "curve" and sm.points:
            render_curve(t, sm)
        elif sm.mode == "numeric" and sm.points:
            render_numeric(t, sm)
        else:
            render_idle(t, sm)
    return t.img


def header(t, sm, label):
    t.fillRect(0, 0, 320, 24, BLUE)
    t.setTextColor(WHITE, BLUE)
    t.setTextSize(1)
    t.setCursor(4, 6)
    t.print(sm.band)
    t.setCursor(80, 6)
    t.print(label)


def render_welcome(t):
    t.fillScreen(BLACK)
    t.fillRect(0, 0, 320, 44, BLUE)
    t.setTextColor(WHITE, BLUE)
    t.setTextSize(2)
    t.setCursor(16, 8)
    t.print("SWR METER")
    t.setTextSize(1)
    t.setCursor(16, 30)
    t.print("Uno R4 + AA-30.ZERO")
    t.setTextColor(WHITE, BLACK)
    t.setTextSize(1)
    rows = [
        (YELLOW, "[BAND]", "Select HF band (160m-10m)"),
        (GREEN, "[START]", "Scan current band"),
        (CYAN, "[MODE]", "Curve / numeric readout"),
        (ORANGE, "[CAL]", "Calibration check"),
    ]
    ys = [58, 84, 110, 136]
    for (col, lbl, desc), y in zip(rows, ys):
        t.setCursor(16, y)
        t.setTextColor(col, BLACK)
        t.print(lbl)
        t.setTextColor(WHITE, BLACK)
        t.setCursor(84, y)
        t.print(desc)
    t.setCursor(16, 172)
    t.setTextColor(LIGHTGREY, BLACK)
    t.print("Press any button to continue")


def render_idle(t, sm):
    header(t, sm, "IDLE")
    t.fillRect(0, 26, 320, 240 - 26, BLACK)
    t.setTextColor(WHITE, BLACK)
    t.setCursor(30, 120)
    t.setTextSize(2)
    t.print("Press [START]")
    t.setCursor(50, 150)
    t.print("to scan")


def render_scanning(t, sm):
    header(t, sm, "SCANNING...")
    t.fillRect(0, 26, 320, 240 - 26, BLACK)
    t.setTextColor(WHITE, BLACK)
    t.setCursor(30, 120)
    t.setTextSize(2)
    t.print("Scanning %s..." % sm.band)


def render_curve(t, sm):
    header(t, sm, "PRESS START")
    lo, hi = BANDS.get(sm.band, (14.0, 14.35))
    x0, x1, y0, y1 = 8, 312, 36, 224
    plotW, plotH = x1 - x0, y1 - y0
    swrMin, swrMax = 1.0, 3.0
    t.fillRect(x0 - 2, y0 - 8, plotW + 4, plotH + 16, BLACK)
    t.drawRect(x0, y0, plotW, plotH, WHITE)
    y2 = y1 - int((2.0 - swrMin) / (swrMax - swrMin) * plotH)
    t.drawLine(x0, y2, x1, y2, DARKGREY)
    t.setTextColor(DARKGREY, BLACK)
    t.setCursor(x1 - 24, y2 - 10)
    t.print("SWR2")
    color = GREEN
    for i in range(1, len(sm.points)):
        fa, sa = sm.points[i - 1]
        fc, sc = sm.points[i]
        ax = x0 + int((fa - lo) / (hi - lo) * plotW)
        cx = x0 + int((fc - lo) / (hi - lo) * plotW)
        ay = y1 - int((sa - swrMin) / (swrMax - swrMin) * plotH)
        cy = y1 - int((sc - swrMin) / (swrMax - swrMin) * plotH)
        ay = max(y0, min(y1, ay))
        cy = max(y0, min(y1, cy))
        color = RED if sc >= 2.0 else (YELLOW if sc >= 1.5 else GREEN)
        t.drawLine(ax, ay, cx, cy, color)
    if sm.points:
        swrMinV = min(p[1] for p in sm.points)
        fMin = min(sm.points, key=lambda p: p[1])[0]
        t.setTextColor(WHITE, BLACK)
        t.setCursor(12, y1 + 6)
        t.print("MinSWR ")
        t.print("%.2f" % swrMinV)
        t.print(" @ ")
        t.print("%.3f" % fMin)
        t.print(" MHz")
        t.setCursor(140, y1 + 6)
        t.print("n=")
        t.print(len(sm.points))


def render_numeric(t, sm):
    header(t, sm, "PRESS START")
    f, r, x, swr = sm.points[-1][0], 50.0, 0.0, sm.points[-1][1]
    # keep last raw measurement for display; R/X from last @POINT if available
    if hasattr(sm, "last_meas"):
        f, r, x, swr = sm.last_meas
    t.fillRect(0, 26, 320, 240 - 26, BLACK)
    t.setTextSize(3)
    t.setTextColor(CYAN, BLACK)
    t.setCursor(10, 40)
    t.print("F ")
    t.print("%.3f" % f)
    t.println(" MHz")
    t.setTextColor(GREEN, BLACK)
    t.setCursor(10, 90)
    t.print("R  ")
    t.print("%.1f" % r)
    t.println(" ohm")
    t.setTextColor(YELLOW, BLACK)
    t.setCursor(10, 140)
    t.print("X  ")
    t.print("%.1f" % x)
    t.println(" ohm")
    t.setTextColor(WHITE, BLACK)
    t.setCursor(10, 190)
    t.print("SWR ")
    t.print("%.2f" % swr)


def render_cal_prompt(t, sm):
    header(t, sm, "CALIBRATE")
    t.fillRect(0, 26, 320, 240 - 26, BLACK)
    t.setTextSize(2)
    t.setTextColor(YELLOW, BLACK)
    t.setCursor(20, 40)
    t.print("CALIBRATE")
    t.setTextSize(1)
    t.setTextColor(WHITE, BLACK)
    t.setCursor(20, 84)
    t.print("Step ")
    t.print("%d" % sm.cal_phase)
    t.print("/")
    t.print(NUM_PHASES)
    t.setCursor(20, 104)
    t.setTextColor(CYAN, BLACK)
    t.print(PROMPTS.get(sm.cal_phase, ""))
    t.setTextColor(WHITE, BLACK)
    t.setCursor(20, 128)
    t.print("Press START to sweep all bands")
    t.setCursor(20, 150)
    t.print("[MODE] cancel")


def render_cal_progress(t, sm):
    header(t, sm, "CAL SWEEP")
    t.fillRect(0, 26, 320, 240 - 26, BLACK)
    t.setTextSize(1)
    t.setTextColor(YELLOW, BLACK)
    t.setCursor(20, 40)
    t.print("Calibrating...")
    t.setTextColor(WHITE, BLACK)
    t.setCursor(20, 64)
    t.print("Ref: ")
    t.setTextColor(CYAN, BLACK)
    t.print(PROMPTS.get(sm.cal_phase, ""))
    t.setTextColor(WHITE, BLACK)
    t.setCursor(20, 90)
    t.print("Band ")
    t.print("%d" % sm.cal_band)
    t.print("/")
    t.print(len(BANDS))
    t.print("  ")
    t.print(list(BANDS.keys())[sm.cal_band - 1])
    t.setCursor(20, 114)
    t.print("Point ")
    t.print("%d" % sm.cal_pt)
    t.print("/")
    t.print(CAL_PTS)
    total_pts = len(BANDS) * CAL_PTS
    done = (sm.cal_band - 1) * CAL_PTS + sm.cal_pt
    bw = 240
    bx, by = 20, 150
    fill = int(float(done) / total_pts * bw)
    if fill > bw:
        fill = bw
    t.drawRect(bx, by, bw, 12, WHITE)
    t.fillRect(bx + 1, by + 1, fill - 1, 10, GREEN)
    t.setCursor(20, 170)
    t.print("%d" % (done * 100 // total_pts))
    t.print(" %  [MODE] cancel")


def render_cal_done(t, sm):
    t.fillRect(0, 0, 320, 240, BLACK)
    t.setTextSize(2)
    t.setTextColor(GREEN if sm.cal_done_pass else RED, BLACK)
    t.setCursor(20, 40)
    t.print("CAL DONE" if sm.cal_done_pass else "CAL FAILED")
    t.setTextSize(1)
    t.setTextColor(WHITE, BLACK)
    t.setCursor(20, 90)
    t.print("Failures: ")
    t.print("%d" % sm.cal_done_fails)
    t.setCursor(20, 110)
    t.print("Correction saved")
    t.setCursor(20, 160)
    t.print("Press any button to exit")


# ---------------------------------------------------------------------------
# Serial parsing
# ---------------------------------------------------------------------------
def parse_telemetry(line, sm):
    """Returns True if the display needs a redraw."""
    if not line.startswith("@"):
        return False
    body = line[1:]
    key, _, val = body.partition(":")
    if key == "STATE":
        sm.state = val
        if val == "DISPLAYING":
            sm.points = []
        if val == "CAL_DONE":
            sm.cal_done_pass = True
            sm.cal_done_fails = 0
        return True
    elif key == "BAND":
        sm.band = val
        return True
    elif key == "MODE":
        sm.mode = val
        return True
    elif key == "POINT":
        parts = val.split(",")
        f, r, x, swr = float(parts[0]), float(parts[1]), float(parts[2]), float(parts[3])
        sm.last_meas = (f, r, x, swr)
        sm.points.append((f, swr))
        return True
    elif key == "CALPHASE":
        n, total = val.split("/")
        sm.cal_phase = int(n)
        return True
    elif key == "CALPROG":
        for kv in val.split(","):
            k, _, v = kv.partition("=")
            if k == "band":
                b, _, nb = v.partition("/")
                sm.cal_band = int(b)
            elif k == "pt":
                p, _, np = v.partition("/")
                sm.cal_pt = int(p)
        return True
    return False


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------
class SimulatorApp:
    SCALE = 2

    def __init__(self, root, port, baud, mock=False):
        self.root = root
        self.root.title("AA-30.ZERO SWR Meter Simulator")
        self.ser = None
        self.port = port
        self.baud = baud
        self.mock = mock
        self.model = ScreenModel()
        self.rx_queue = queue.Queue()
        self.paused = False

        self._build_ui()

        if mock:
            self._log("MOCK mode: simulating firmware locally (no serial).")
            threading.Thread(target=self._mock_loop, daemon=True).start()
        elif port:
            self.connect()

        self.root.after(50, self._poll)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # -- mock firmware: replay a boot + scan + calibration demo -----------
    def _mock_loop(self):
        import time, math
        time.sleep(0.3)
        self.rx_queue.put("@STATE:WELCOME")
        time.sleep(1.2)
        self.rx_queue.put("@STATE:IDLE")
        self.rx_queue.put("@BAND:20m")
        self.rx_queue.put("@MODE:curve")
        time.sleep(1.2)
        # emulate START press -> scan
        self.rx_queue.put("@STATE:SCANNING")
        lo, hi = 14.0, 14.35
        for i in range(20):
            f = lo + (hi - lo) * i / 19
            swr = 1.0 + 0.12 * abs(math.sin((f - 14.2) * 18.0)) ** 1.5 + 0.05
            self.rx_queue.put("@POINT:%.6f,50.0,0.0,%.3f" % (f, swr))
            time.sleep(0.05)
        self.rx_queue.put("@STATE:DISPLAYING")
        time.sleep(2.0)
        # emulate calibration wizard
        self.rx_queue.put("@STATE:CALIBRATE")
        self.rx_queue.put("@CALPHASE:1/3")
        time.sleep(1.0)
        for b in range(1, 11):
            for p in range(1, 21):
                self.rx_queue.put("@CALPROG:band=%d/10,pt=%d/20" % (b, p))
                time.sleep(0.02)
        self.rx_queue.put("@CALPHASE:2/3")
        time.sleep(0.8)
        self.rx_queue.put("@CALPHASE:3/3")
        time.sleep(0.8)
        self.rx_queue.put("@STATE:CAL_DONE")

    def _build_ui(self):
        top = ttk.Frame(self.root, padding=6)
        top.grid(row=0, column=0, sticky="nsew")

        self.port_var = tk.StringVar(value=self.port or "")
        ttk.Label(top, text="Port:").grid(row=0, column=0, sticky="w")
        self.port_entry = ttk.Entry(top, textvariable=self.port_var, width=12)
        self.port_entry.grid(row=0, column=1, padx=4)
        ttk.Label(top, text="Baud:").grid(row=0, column=2, sticky="w")
        self.baud_var = tk.StringVar(value=str(self.baud))
        ttk.Entry(top, textvariable=self.baud_var, width=8).grid(row=0, column=3, padx=4)
        self.connect_btn = ttk.Button(top, text="Connect", command=self._toggle_conn)
        self.connect_btn.grid(row=0, column=4, padx=8)

        # Reset the MCU (1200 baud touch) on connect so the welcome screen shows.
        self.reset_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(top, text="Reset on connect", variable=self.reset_var).grid(
            row=0, column=5, padx=8)

        # Display canvas (scaled)
        self.display_img = None
        self.canvas = tk.Canvas(top, width=W * self.SCALE, height=H * self.SCALE,
                                bg="black", highlightthickness=1,
                                highlightbackground="#444")
        self.canvas.grid(row=1, column=0, columnspan=6, pady=6)

        # Soft buttons
        btns = ttk.Frame(top)
        btns.grid(row=2, column=0, columnspan=6, pady=4)
        for label, cmd in [
            ("[ BAND ]", "!BTN:BAND"),
            ("[ START ]", "!BTN:START"),
            ("[ MODE ]", "!BTN:MODE"),
            ("[ CAL ]", "!BTN:CAL"),
        ]:
            b = ttk.Button(btns, text=label, width=10,
                           command=lambda c=cmd: self.send(c))
            b.pack(side="left", padx=6)

        # Log
        logframe = ttk.LabelFrame(top, text="Serial log", padding=4)
        logframe.grid(row=3, column=0, columnspan=6, sticky="nsew", pady=6)

        logwrap = ttk.Frame(logframe)
        logwrap.pack(side="left", fill="both", expand=True)
        self.log = tk.Text(logwrap, height=12, width=100, state="disabled",
                           wrap="word", font=("Consolas", 9),
                           yscrollcommand=None)
        self.log.pack(side="left", fill="both", expand=True)
        sb = ttk.Scrollbar(logwrap, command=self.log.yview)
        sb.pack(side="right", fill="y")
        self.log.configure(yscrollcommand=sb.set)

        btns = ttk.Frame(logframe)
        btns.pack(side="top", fill="x", pady=(4, 0))
        ttk.Button(btns, text="Clear", command=self._clear_log).pack(side="left")
        ttk.Button(btns, text="Pause", command=self._toggle_pause).pack(side="left", padx=6)

    def _log(self, text):
        ts = time.strftime("%H:%M:%S")
        self.log.configure(state="normal")
        self.log.insert("end", f"[{ts}] {text}\n")
        # Only auto-scroll when not paused, so the user can scroll back manually.
        if not self.paused:
            self.log.see("end")
        self.log.configure(state="disabled")

    def _clear_log(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _toggle_pause(self):
        self.paused = not self.paused
        self._log("-- log paused --" if self.paused else "-- log resumed --")

    def connect(self):
        if self.reset_var.get():
            self._reset_target()
        try:
            self.ser = serial.Serial(self.port_var.get(),
                                     int(self.baud_var.get()), timeout=0.1)
            self.connect_btn.configure(text="Disconnect")
            self._log(f"Connected to {self.port_var.get()} @ {self.baud_var.get()}")
            self.send("!GET:STATE")
        except Exception as exc:
            self._log(f"Connect failed: {exc}")
            self.ser = None

    def _reset_target(self):
        """War-reset the Uno R4 by reopening at 1200 baud (Arduino 1200bps
        touch convention), so the firmware reboots and shows the welcome
        screen."""
        port = self.port_var.get()
        try:
            self._log(f"Resetting {port} (1200 baud touch)...")
            temp = serial.Serial(port, 1200, timeout=0.1)
            temp.dtr = False
            temp.close()
            time.sleep(0.5)  # let the bootloader run
        except Exception as exc:
            self._log(f"Reset touch failed: {exc}")

    def disconnect(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
        self.connect_btn.configure(text="Connect")

    def _toggle_conn(self):
        if self.mock:
            self._log("MOCK mode: serial buttons disabled")
            return
        if self.ser:
            self.disconnect()
        else:
            self.connect()

    def send(self, cmd):
        if self.mock:
            self._log(">> " + cmd + " (ignored in MOCK mode)")
            return
        if not self.ser:
            self._log("Not connected")
            return
        try:
            self.ser.write((cmd + "\n").encode())
            self._log(f">> {cmd}")
        except Exception as exc:
            self._log(f"Send failed: {exc}")

    def _read_serial(self):
        if not self.ser:
            return
        try:
            while True:
                line = self.ser.readline()
                if not line:
                    break
                try:
                    text = line.decode("utf-8", "replace").rstrip("\r\n")
                except Exception:
                    text = str(line)
                self.rx_queue.put(text)
        except Exception:
            pass

    def _poll(self):
        try:
            self._read_serial()
        except Exception:
            pass
        changed = False
        while True:
            try:
                line = self.rx_queue.get_nowait()
            except queue.Empty:
                break
            if line:
                self._log("<  " + line)
                if parse_telemetry(line, self.model):
                    changed = True
        if changed:
            self._redraw()
        self.root.after(50, self._poll)

    def _redraw(self):
        img = render(self.model)
        photo = ImageTk.PhotoImage(img.resize((W * self.SCALE, H * self.SCALE),
                                              Image.NEAREST))
        self.display_img = photo
        self.canvas.create_image(0, 0, anchor="nw", image=photo)

    def _on_close(self):
        self.disconnect()
        self.root.destroy()


def main():
    ap = argparse.ArgumentParser(description="AA-30.ZERO SWR meter simulator")
    ap.add_argument("--port", default="", help="Serial port (e.g. COM7)")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate")
    ap.add_argument("--mock", action="store_true",
                    help="Offline demo: simulate firmware locally (no serial)")
    args = ap.parse_args()

    root = tk.Tk()
    SimulatorApp(root, args.port, args.baud, mock=args.mock)
    root.mainloop()


if __name__ == "__main__":
    main()