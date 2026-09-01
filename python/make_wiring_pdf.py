"""Render the Uno R4 / AA-30.ZERO / LCDWIKI MSP4021 display wiring diagram to docs/display_wiring.pdf."""
import os
import time
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(BASE, "docs", "display_wiring.pdf")

C_U5V = "#b71c1c"
C_GND = "#37474f"
C_SIG = "#1f4f8f"
C_TCH = "#7b1fa2"
C_PWR = "#1b5e20"

fig, ax = plt.subplots(figsize=(16, 10), dpi=150)
ax.set_xlim(0, 100)
ax.set_ylim(0, 90)
ax.axis("off")

# Display signal lanes (LCD SPI, 5-wire) from R4 to the MSP4021.
LANES = [
    ("CS",    62, "D10",  "CS"),
    ("RESET", 55, "D8",   "RESET"),
    ("DC/RS", 48, "D9",   "DC/RS"),
    ("SDI(MOSI)", 41, "D11", "SDI"),
    ("SCK",   34, "D13",  "SCK"),
    ("SDO(MISO)", 27, "D12", "SDO"),
]
# Touch signal lanes (XPT2046, shares SPI bus; only CS/IRQ need their own pins).
TOUCH_LANES = [
    ("T_CS",  62, "D6",  "T_CS"),
    ("T_IRQ", 55, "A0",  "T_IRQ"),
]
Y_5V, Y_GND = 18, 12
BOX_TOP, BOX_BOT = 68, 9

BLOCK_X = {"r4": (2.0, 16.0), "lcd": (62.0, 21.0)}
R4_R = BLOCK_X["r4"][0] + BLOCK_X["r4"][1]
LCD_L = BLOCK_X["lcd"][0]


def block(key, title, sub, ec):
    x, w = BLOCK_X[key]
    ax.add_patch(FancyBboxPatch((x, BOX_BOT), w, BOX_TOP - BOX_BOT,
                                boxstyle="round,pad=0.3,rounding_size=1.1",
                                linewidth=1.6, edgecolor=ec, facecolor="white", zorder=2))
    ax.text(x + w / 2, BOX_TOP - 3.0, title, ha="center", va="center",
            fontsize=12.5, fontweight="bold", color="#263238", zorder=4)
    ax.text(x + w / 2, BOX_TOP - 7.0, sub, ha="center", va="center",
            fontsize=8.2, color="#455a64", zorder=4)


block("r4", "Uno R4 Minima", "Renesas RA4M1  |  5 V", "#78909c")
block("lcd", 'LCDWIKI MSP4021 4.0"', "ST7796S | 480x320\nXPT2046 resistive touch", "#00695c")


def wire(x1, y1, x2, y2, color, lw=1.7, z=3):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="->", mutation_scale=10,
                                 linewidth=lw, color=color, shrinkA=0, shrinkB=0, zorder=z))


def tag(x, y, text, color, ha="center", fs=8.0, weight="bold"):
    ax.text(x, y, text, fontsize=fs, color=color, fontweight=weight, ha=ha, va="center",
            zorder=7, bbox=dict(boxstyle="round,pad=0.13", fc="white", ec="none"))


# --- LCD SPI signal lanes --------------------------------------------------
for name, y, r4pin, lcdpin in LANES:
    wire(R4_R, y, LCD_L, y, C_SIG, 1.7)
    tag((R4_R + LCD_L) / 2, y + 1.7, f"{name}", C_SIG)
    tag(R4_R - 0.6, y, r4pin, C_SIG, ha="right", fs=8.2)
    tag(LCD_L + 1.3, y, lcdpin, C_SIG, ha="left", fs=8.2)

# --- touch signal lanes (lower) --------------------------------------------
for name, y, r4pin, tpin in TOUCH_LANES:
    wire(R4_R + 3, y, LCD_L, y, C_TCH, 1.7)
    tag((R4_R + 3 + LCD_L) / 2, y + 1.7, f"{name}", C_TCH)
    tag(R4_R + 3 - 0.6, y, r4pin, C_TCH, ha="right", fs=8.2)
    tag(LCD_L + 1.3, y, tpin, C_TCH, ha="left", fs=8.2)
# note: T_DIN/T_DO/T_CLK reuse MOSI/SCK/MISO (shared SPI bus) - shown as a note.

# --- power / ground --------------------------------------------------------
wire(R4_R, Y_5V, LCD_L, Y_5V, C_U5V, 2.2)
tag((R4_R + LCD_L) / 2, Y_5V - 2.2, "VCC 5 V", C_U5V)
tag(LCD_L + 1.3, Y_5V, "VCC", C_U5V, ha="left")
wire(R4_R, Y_GND, LCD_L, Y_GND, C_GND, 2.2)
tag((R4_R + LCD_L) / 2, Y_GND - 1.8, "GND", C_GND)
tag(LCD_L + 1.3, Y_GND, "GND", C_GND, ha="left")

# --- heading -----------------------------------------------------------
ax.text(2.0, 86.0, "Uno R4 Minima \N{RIGHTWARDS ARROW} LCDWIKI MSP4021 4.0\" TFT  \N{MIDDLE DOT}  LiPower battery shield",
        ha="left", va="center", fontsize=13.5, fontweight="bold", color="#263238")
ax.text(2.0, 83.0, "MSP4021 (ST7796S, 480x320)  \N{MIDDLE DOT}  XPT2046 resistive touch (shared SPI)  \N{MIDDLE DOT}  LCDWIKI Arduino wiring, remapped for Uno R4",
        ha="left", va="center", fontsize=9, color="#455a64")

# --- footnotes ---------------------------------------------------------
ax.text(2.0, 79.0, "AA-30.ZERO:  UART1 TX \N{RIGHTWARDS ARROW} R4 D0 (Serial1 RX)  \N{MIDDLE DOT}  UART1 RX \N{RIGHTWARDS ARROW} R4 D1 (Serial1 TX)  \N{MIDDLE DOT}  GND common.",
        ha="left", va="center", fontsize=8.2, color="#37474f")
ax.text(2.0, 76.0, "Vendor UNO map uses A4 (RESET) and A5 (CS); on the R4 those are the I2C pins (battery gauge) so they are moved to D8/D10.",
        ha="left", va="center", fontsize=8.0, color="#b71c1c")
ax.text(2.0, 73.0, "MSP4021 pins: VCC GND CS RESET DC/RS SDI(MOSI) SCK LED SDO(MISO)  \N{MIDDLE DOT}  touch: T_CLK T_CS T_DIN T_DO T_IRQ.",
        ha="left", va="center", fontsize=8.0, color="#37474f")
ax.text(2.0, 70.0, "The module shares the SPI bus between display and touch (SJ1-SJ3 jumpered): T_DIN=MOSI D11, T_DO=MISO D12, T_CLK=SCK D13; only T_CS (D6) and T_IRQ (A0) are unique.",
        ha="left", va="center", fontsize=8.0, color="#7b1fa2")

# --- legend ----------------------------------------------------------
handles = [
    plt.Line2D([0], [0], color=C_SIG, lw=1.8, label="LCD SPI (direct)"),
    plt.Line2D([0], [0], color=C_TCH, lw=1.8, label="Touch (shared SPI + CS/IRQ)"),
    plt.Line2D([0], [0], color=C_U5V, lw=2.2, label="VCC 5 V"),
    plt.Line2D([0], [0], color=C_GND, lw=2.2, label="GND"),
]
ax.legend(handles=handles, loc="center left", bbox_to_anchor=(0.005, 0.74),
          ncol=2, fontsize=8, frameon=False)

# --- LiPower battery shield inset (right) ---------------------------------
BX, BY, BW, BH = 62, 88, 34, 0  # placeholder (unused, kept for structure)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
tmp = os.path.join(os.path.dirname(OUT), "display_wiring.tmp.pdf")
fig.savefig(tmp, format="pdf", bbox_inches="tight")
for _ in range(25):
    try:
        os.replace(tmp, OUT)
        break
    except PermissionError:
        time.sleep(0.2)
else:
    print("WARN: could not replace", OUT, "- it may be open in a viewer")
fig.savefig(os.path.join(os.path.dirname(OUT), "wiring_preview.png"), format="png", bbox_inches="tight")
print("PDF:", OUT)
