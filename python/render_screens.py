"""Render the firmware's ILI9341 screens to PNG using the actual Adafruit
5x7 bitmap font (pixel-exact text).

Parses glcdfont.c (the 'classic' Adafruit_GFX font: 256 glyphs x 5 bytes,
each byte is a 7-bit column, bit 0 = top) and renders each char as a 5x7
glyph in a 6x8 cell, scaled by textSize -- exactly as Adafruit_GFX does.

Screens rendered (one PNG each into media/screens/):
  welcome, idle, scanning, curve, numeric, calibrate, cal_pass, cal_fail
"""
import math
import os
import re
from PIL import Image, ImageDraw

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(BASE, "media", "screens")
FONT_SRC = os.path.join(
    BASE, ".pio", "libdeps", "uno_r4_minima", "Adafruit GFX Library", "glcdfont.c"
)
os.makedirs(OUT_DIR, exist_ok=True)

W, H = 320, 240

# ---- ILI9341 16-bit colours (as used in the firmware) ------------------
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
    """Parse glcdfont.c into a list of 256 glyphs (each 5x7 bit mask)."""
    src = open(FONT_SRC, encoding="utf-8").read()
    block = src[src.index("font[]") : src.index("};", src.index("font[]"))]
    hexes = re.findall(r"0x([0-9A-Fa-f]{2})", block)
    assert len(hexes) == 256 * 5, f"expected 1280 bytes, got {len(hexes)}"
    glyphs = []
    for g in range(256):
        cols = [int(hexes[g * 5 + c], 16) for c in range(5)]
        glyph = [[(cols[c] >> row) & 1 for c in range(5)] for row in range(7)]
        glyphs.append(glyph)
    return glyphs


GLYPHS = load_glcdfont()


class TFT:
    """Emulator of the subset of Adafruit_GFX used by the firmware, using the
    real 5x7 bitmap font and the 6x8-per-char advance rule."""

    def __init__(self):
        self.img = Image.new("RGB", (W, H), BLACK)
        self.d = ImageDraw.Draw(self.img)
        self.text_color = WHITE
        self.bg_color = BLACK
        self.text_size = 1
        self.cursor = (0, 0)

    def fillScreen(self, color):
        self.d.rectangle([0, 0, W - 1, H - 1], fill=color)

    def fillRect(self, x, y, w, h, color):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=color)

    def drawRect(self, x, y, w, h, color):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], outline=color)

    def drawLine(self, x0, y0, x1, y1, color):
        self.d.line([x0, y0, x1, y1], fill=color)

    def setTextColor(self, fg, bg=None):
        self.text_color = fg
        self.bg_color = bg if bg is not None else fg

    def setTextSize(self, s):
        self.text_size = s

    def setCursor(self, x, y):
        self.cursor = (x, y)

    def _draw_char(self, ch):
        """Draw one char at the cursor, advance 6*size px (Adafruit rule)."""
        n = self.text_size
        cx, cy = self.cursor
        glyph = GLYPHS[ord(ch) % 256]
        for row in range(7):
            for col in range(5):
                if glyph[row][col]:
                    x0 = cx + col * n
                    y0 = cy + row * n
                    self.d.rectangle([x0, y0, x0 + n - 1, y0 + n - 1],
                                     fill=self.text_color)
        # background fill for the 6x8 cell (GFX does this when bg set and != fg)
        if self.bg_color != self.text_color:
            self.d.rectangle([cx, cy, cx + 5 * n - 1, cy + 7 * n - 1],
                             fill=self.bg_color)
            # re-draw glyph on top
            for row in range(7):
                for col in range(5):
                    if glyph[row][col]:
                        x0 = cx + col * n
                        y0 = cy + row * n
                        self.d.rectangle([x0, y0, x0 + n - 1, y0 + n - 1],
                                         fill=self.text_color)
        self.cursor = (cx + 6 * n, cy)

    def _render(self, s):
        for ch in s:
            self._draw_char(ch)

    def print(self, *args):
        for a in args:
            self._render(str(a))

    def println(self, *args):
        for a in args:
            self._render(str(a) + "\n")  # '\n' glyph is blank (ASCII 10)
        x, y = self.cursor
        self.cursor = (x, y + 8 * self.text_size)


def new_canvas():
    return TFT()


# ---------------------------------------------------------------------------
# Firmware screens (mirrors src/main.cpp exactly)
# ---------------------------------------------------------------------------

def draw_welcome(t):
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

    t.setCursor(16, 58)
    t.setTextColor(YELLOW, BLACK)
    t.print("[BAND]")
    t.setTextColor(WHITE, BLACK)
    t.setCursor(84, 58)
    t.print("Select HF band (160m-10m)")

    t.setCursor(16, 84)
    t.setTextColor(GREEN, BLACK)
    t.print("[START]")
    t.setTextColor(WHITE, BLACK)
    t.setCursor(84, 84)
    t.print("Scan current band")

    t.setCursor(16, 110)
    t.setTextColor(CYAN, BLACK)
    t.print("[MODE]")
    t.setTextColor(WHITE, BLACK)
    t.setCursor(84, 110)
    t.print("Curve / numeric readout")

    t.setCursor(16, 136)
    t.setTextColor(ORANGE, BLACK)
    t.print("[CAL]")
    t.setTextColor(WHITE, BLACK)
    t.setCursor(84, 136)
    t.print("Calibration check")

    t.setCursor(16, 172)
    t.setTextColor(LIGHTGREY, BLACK)
    t.print("Press any button to continue")


def draw_header(t, band, state, cal_active=False):
    t.fillRect(0, 0, 320, 24, BLUE)
    t.setTextColor(WHITE, BLUE)
    t.setTextSize(1)
    t.setCursor(4, 6)
    t.print(band)
    t.setCursor(80, 6)
    if state == "WELCOME":
        t.print("WELCOME")
    elif state == "IDLE":
        t.print("IDLE")
    elif state == "CALIBRATE":
        t.print("CALIBRATE")
    elif state == "SCANNING":
        t.print("SCANNING...")
    elif state == "DISPLAYING":
        t.print("CAL RESULT" if cal_active else "PRESS START")


def draw_idle(t, band):
    draw_header(t, band, "IDLE")
    t.fillRect(0, 26, 320, 240 - 26, BLACK)
    t.setTextColor(WHITE, BLACK)
    t.setCursor(30, 120)
    t.setTextSize(2)
    t.print("Press [START]")
    t.setCursor(50, 150)
    t.print("to scan")


def draw_scanning(t, band):
    draw_header(t, band, "SCANNING")
    t.fillRect(0, 26, 320, 240 - 26, BLACK)
    t.setTextColor(WHITE, BLACK)
    t.setCursor(30, 120)
    t.setTextSize(2)
    t.print("Scanning 20m...")
    t.setCursor(40, 150)
    t.setTextSize(1)
    t.print("center=14250000 span=350000")


def draw_curve(t, band, points):
    x0, x1, y0, y1 = 8, 312, 36, 224
    plotW = x1 - x0
    plotH = y1 - y0
    swrMin, swrMax = 1.0, 3.0
    low_mhz = 14000000 / 1e6
    hi_mhz = 14350000 / 1e6

    draw_header(t, band, "DISPLAYING")
    t.fillRect(x0 - 2, y0 - 8, plotW + 4, plotH + 16, BLACK)
    t.drawRect(x0, y0, plotW, plotH, WHITE)

    y2 = y1 - int((2.0 - swrMin) / (swrMax - swrMin) * plotH)
    t.drawLine(x0, y2, x1, y2, DARKGREY)
    t.setTextColor(DARKGREY, BLACK)
    t.setCursor(x1 - 24, y2 - 10)
    t.print("SWR2")

    color = GREEN
    for i in range(1, len(points)):
        fa, sa = points[i - 1]
        fc, sc = points[i]
        ax = x0 + int((fa - low_mhz) / (hi_mhz - low_mhz) * plotW)
        cx = x0 + int((fc - low_mhz) / (hi_mhz - low_mhz) * plotW)
        ay = y1 - int((sa - swrMin) / (swrMax - swrMin) * plotH)
        cy = y1 - int((sc - swrMin) / (swrMax - swrMin) * plotH)
        ay = max(y0, min(y1, ay))
        cy = max(y0, min(y1, cy))
        color = RED if sc >= 2.0 else (YELLOW if sc >= 1.5 else GREEN)
        t.drawLine(ax, ay, cx, cy, color)

    swrMinV = min(p[1] for p in points)
    fMin = min(points, key=lambda p: p[1])[0]
    t.setTextColor(WHITE, BLACK)
    t.setCursor(12, y1 + 6)
    t.print("MinSWR ")
    t.print("%.2f" % swrMinV)
    t.print(" @ ")
    t.print("%.3f" % fMin)
    t.print(" MHz")
    t.setCursor(140, y1 + 6)
    t.print("n=")
    t.print(len(points))


def draw_numeric(t, band, m):
    draw_header(t, band, "DISPLAYING")
    t.fillRect(0, 26, 320, 240 - 26, BLACK)

    t.setTextSize(3)
    t.setTextColor(CYAN, BLACK)
    t.setCursor(10, 40)
    t.print("F ")
    t.print("%.3f" % m["f"])
    t.println(" MHz")

    t.setTextColor(GREEN, BLACK)
    t.setCursor(10, 90)
    t.print("R  ")
    t.print("%.1f" % m["r"])
    t.println(" ohm")

    t.setTextColor(YELLOW, BLACK)
    t.setCursor(10, 140)
    t.print("X  ")
    t.print("%.1f" % m["x"])
    t.println(" ohm")

    t.setTextColor(WHITE, BLACK)
    t.setCursor(10, 190)
    t.print("SWR ")
    t.print("%.2f" % m["swr"])


def draw_cal_prompt(t, band, phase_name, step, total):
    draw_header(t, band, "CALIBRATE")
    t.fillRect(0, 26, 320, 240 - 26, BLACK)
    t.setTextSize(2)
    t.setTextColor(YELLOW, BLACK)
    t.setCursor(20, 40)
    t.print("CALIBRATE")
    t.setTextSize(1)
    t.setTextColor(WHITE, BLACK)
    t.setCursor(20, 84)
    t.print("Step ")
    t.print("%d" % step)
    t.print("/")
    t.print("%d" % total)
    t.setCursor(20, 104)
    t.setTextColor(CYAN, BLACK)
    t.print(phase_name)
    t.setTextColor(WHITE, BLACK)
    t.setCursor(20, 128)
    t.print("Press START to sweep all bands")
    t.setCursor(20, 150)
    t.print("[MODE] cancel")


def draw_cal_progress(t, band, phase_name, band_idx, n_bands, pt, pts):
    draw_header(t, band, "CAL SWEEP")
    t.fillRect(0, 26, 320, 240 - 26, BLACK)
    t.setTextSize(1)
    t.setTextColor(YELLOW, BLACK)
    t.setCursor(20, 40)
    t.print("Calibrating...")
    t.setTextColor(WHITE, BLACK)
    t.setCursor(20, 64)
    t.print("Ref: ")
    t.setTextColor(CYAN, BLACK)
    t.print(phase_name)
    t.setTextColor(WHITE, BLACK)
    t.setCursor(20, 90)
    t.print("Band ")
    t.print("%d" % band_idx)
    t.print("/")
    t.print("%d" % n_bands)
    t.print("  ")
    t.print(band)
    t.setCursor(20, 114)
    t.print("Point ")
    t.print("%d" % pt)
    t.print("/")
    t.print("%d" % pts)

    total_pts = n_bands * pts
    done = (band_idx - 1) * pts + pt
    bw = 240
    bx, by = 20, 150
    fill = int(float(done) / total_pts * bw)
    if fill > bw:
        fill = bw
    t.drawRect(bx, by, bw, 12, WHITE)
    t.fillRect(bx + 1, by + 1, fill - 1, 10, GREEN)
    t.setCursor(20, 170)
    t.print("%d" % (done * 100 / total_pts))
    t.print(" %  [MODE] cancel")


def draw_cal_done(t, passed, fails, valid):
    t.fillRect(0, 0, 320, 240, BLACK)
    t.setTextSize(2)
    t.setTextColor(GREEN if passed else RED, BLACK)
    t.setCursor(20, 40)
    t.print("CAL DONE" if passed else "CAL FAILED")
    t.setTextSize(1)
    t.setTextColor(WHITE, BLACK)
    t.setCursor(20, 90)
    t.print("Failures: ")
    t.print("%d" % fails)
    t.setCursor(20, 110)
    t.print("Correction saved" if valid else "Correction NOT saved")
    t.setCursor(20, 160)
    t.print("Press any button to exit")


# ---------------------------------------------------------------------------
# Sample data
# ---------------------------------------------------------------------------
def synth_curve():
    pts = []
    lo, hi = 14.0, 14.35
    n = 40
    for i in range(n):
        f = lo + (hi - lo) * i / (n - 1)
        swr = 1.0 + 0.12 * abs(math.sin((f - 14.2) * 18.0)) ** 1.5 + 0.05
        pts.append((round(f, 4), round(swr, 3)))
    return pts


def save(t, name):
    path = os.path.join(OUT_DIR, name + ".png")
    t.img.save(path)
    print(name, "->", path)


if __name__ == "__main__":
    t = new_canvas()
    draw_welcome(t)
    save(t, "welcome")

    t = new_canvas()
    draw_idle(t, "20m")
    save(t, "idle")

    t = new_canvas()
    draw_scanning(t, "20m")
    save(t, "scanning")

    t = new_canvas()
    draw_curve(t, "20m", synth_curve())
    save(t, "curve")

    t = new_canvas()
    draw_numeric(t, "20m", {"f": 14.150, "r": 50.4, "x": -0.2, "swr": 1.02})
    save(t, "numeric")

    # Calibration wizard screens
    t = new_canvas()
    draw_cal_prompt(t, "20m", "Connect 50 ohm load", 1, 3)
    save(t, "calibrate")

    t = new_canvas()
    draw_cal_progress(t, "20m", "Connect 50 ohm load", 3, 10, 12, 20)
    save(t, "cal_progress")

    t = new_canvas()
    draw_cal_prompt(t, "20m", "Connect SHORT", 2, 3)
    save(t, "cal_short")

    t = new_canvas()
    draw_cal_done(t, True, 0, True)
    save(t, "cal_done")

    print("Done ->", OUT_DIR)