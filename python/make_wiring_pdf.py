"""Render the Uno R4 / AA-30.ZERO / DFR0669 display wiring diagram to docs/display_wiring.pdf."""
import os
import time
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(BASE, "docs", "display_wiring.pdf")

C_U5V = "#b71c1c"
C_33 = "#1b5e20"
C_GND = "#37474f"
C_SIG = "#1f4f8f"
C_TCH = "#7b1fa2"
C_RES = "#8d6e63"

fig, ax = plt.subplots(figsize=(16, 11), dpi=150)
ax.set_xlim(0, 100)
ax.set_ylim(0, 96)
ax.axis("off")

# Signal lanes: SPI (5-wire) drawn horizontally from R4 to the DFR0669.
LANES = [
    ("RST", 62, "D8",  "RES"),
    ("DC",  55, "D9",  "DC"),
    ("CS",  48, "D10", "CS"),
    ("CLK", 41, "D13", "SCLK"),
    ("DIN", 34, "D11", "MOSI"),
]
Y_5V, Y_GND = 24, 18
BOX_TOP, BOX_BOT = 70, 14

BLOCK_X = {"r4": (2.0, 16.0), "lcd": (62.0, 20.0)}
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
block("lcd", 'DFRobot DFR0669 3.5" TFT', "ILI9488 | 480x320\n3.3-5.5 V  |  GT911 touch", "#00695c")


def wire(x1, y1, x2, y2, color, lw=1.7, z=3):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="->", mutation_scale=10,
                                 linewidth=lw, color=color, shrinkA=0, shrinkB=0, zorder=z))


def tag(x, y, text, color, ha="center", fs=8.0, weight="bold"):
    ax.text(x, y, text, fontsize=fs, color=color, fontweight=weight, ha=ha, va="center",
            zorder=7, bbox=dict(boxstyle="round,pad=0.13", fc="white", ec="none"))


# --- SPI signal lanes (direct 3.3-5.5 V, no level shifter) -----------------
for name, y, r4pin, lcdpin in LANES:
    wire(R4_R, y, LCD_L, y, C_SIG, 1.7)
    tag((R4_R + LCD_L) / 2, y + 1.7, f"{name}", C_SIG)
    tag(R4_R - 0.6, y, r4pin, C_SIG, ha="right", fs=8.2)
    tag(LCD_L + 1.3, y, lcdpin, C_SIG, ha="left", fs=8.2)

# --- power / ground nets (direct) ------------------------------------------
for y, label, color in [(Y_5V, "VCC 3.3-5.5 V", C_U5V), (Y_GND, "GND", C_GND)]:
    wire(R4_R, y, LCD_L, y, color, 2.2)
    tag((R4_R + LCD_L) / 2, y - 2.2, label, color)
    tag(LCD_L + 1.3, y, "VCC" if label.startswith("VCC") else "GND", color, ha="left")

# --- touch I2C (separate net, lower) ----------------------------------------
TCH_Y = 11
wire(R4_R + 6, TCH_Y, LCD_L, TCH_Y, C_TCH, 1.7)  # SDA / SCL pair drawn as one net
tag((R4_R + 6 + LCD_L) / 2, TCH_Y + 1.6, "Touch I2C  SDA=A4  SCL=A5  (GT911 @0x5D)", C_TCH)
tag(R4_R + 6 - 0.6, TCH_Y, "A4/A5", C_TCH, ha="right", fs=8.0)
tag(LCD_L + 1.3, TCH_Y, "SDA/SCL", C_TCH, ha="left", fs=8.0)

# --- heading ----------------------------------------------------------
ax.text(2.0, 92.0, "Uno R4 Minima \N{RIGHTWARDS ARROW} DFRobot DFR0669 TFT  \N{MIDDLE DOT}  LiPower battery shield",
        ha="left", va="center", fontsize=13.5, fontweight="bold", color="#263238")
ax.text(2.0, 89.0, "DFRobot DFR0669 (ILI9488, 480x320, 3.3-5.5 V)  \N{MIDDLE DOT}  GT911 touch I2C  \N{MIDDLE DOT}  LiPower shield (3.7 V LiPo \N{RIGHTWARDS ARROW} 5 V)",
        ha="left", va="center", fontsize=9, color="#455a64")

# --- LiPower battery shield inset (right, below the TFT block) -------------
BX, BY, BW, BH = 46, 71, 46, 10
ax.add_patch(FancyBboxPatch((BX, BY), BW, BH,
                            boxstyle="round,pad=0.3,rounding_size=0.9",
                            linewidth=1.2, edgecolor="#1b5e20", facecolor="#f2f7f2", zorder=2))
ax.text(BX + BW / 2, BY + BH - 1.6, "LiPower Shield 0.5A  (power + fuel gauge)",
        ha="center", va="center", fontsize=9, fontweight="bold", color="#1b5e20", zorder=3)
ax.text(BX + BW / 2, BY + 5.2, "3.7 V LiPo \N{RIGHTWARDS ARROW} 5 V  \N{MIDDLE DOT}  MAX17043 gauge \N{RIGHTWARDS ARROW} I2C A4/A5 @0x36",
        ha="center", va="center", fontsize=8.2, color="#455a64", zorder=3)
ax.text(BX + BW / 2, BY + 3.0, "low-battery ALRT \N{RIGHTWARDS ARROW} D2  \N{MIDDLE DOT}  charges LiPo via mini-USB (500 mA)",
        ha="center", va="center", fontsize=8.2, color="#455a64", zorder=3)

# --- footnotes ---------------------------------------------------------
ax.text(2.0, 85.0, "AA-30.ZERO:  UART1 TX \N{RIGHTWARDS ARROW} R4 D0 (Serial1 RX)  \N{MIDDLE DOT}  UART1 RX \N{RIGHTWARDS ARROW} R4 D1 (Serial1 TX)  \N{MIDDLE DOT}  GND common.",
        ha="left", va="center", fontsize=8.2, color="#37474f")
ax.text(2.0, 82.0, "Module runs at 3.3-5.5 V, so SPI logic needs NO level shifter (earlier 2.4\" ILI9341 + LEVEL-8P + HU1-HU8 are removed). Backlight (BL) is on by default.",
        ha="left", va="center", fontsize=8.0, color="#37474f")
ax.text(2.0, 79.0, "Display uses SPI D8\N{EN DASH}D13; touch uses I2C A4/A5; AA-30 uses D0/D1 \N{EM DASH} no overlap. Touch INT and SDCS are not used.",
        ha="left", va="center", fontsize=8.0, color="#37474f")
ax.text(2.0, 76.0, "Display: SPI D8-D13  \N{MIDDLE DOT}  touch: I2C A4/A5  \N{MIDDLE DOT}  AA-30: D0/D1  \N{EM DASH} no overlap.  MISO optional.",
        ha="left", va="center", fontsize=8.0, color="#37474f")
ax.text(2.0, 73.0, "LiPower: 5 V powers R4 + DFR0669; MAX17043 gauge shares I2C A4/A5 (0x36) with GT911 (0x5D); low-batt ALRT on D2.",
        ha="left", va="center", fontsize=8.0, color="#1b5e20")

# --- legend ----------------------------------------------------------
handles = [
    plt.Line2D([0], [0], color=C_SIG, lw=1.8, label="SPI logic (direct)"),
    plt.Line2D([0], [0], color=C_TCH, lw=1.8, label="Touch I2C"),
    plt.Line2D([0], [0], color=C_U5V, lw=2.2, label="VCC 3.3-5.5 V"),
    plt.Line2D([0], [0], color=C_GND, lw=2.2, label="GND"),
]
ax.legend(handles=handles, loc="center left", bbox_to_anchor=(0.005, 0.60),
          ncol=2, fontsize=8, frameon=False)

# --- inset: GT911 touch controller (I2C) ------------------------------------
IX, IY, IW, IH = 20, 2, 46, 7
ax.add_patch(FancyBboxPatch((IX, IY), IW, IH,
                            boxstyle="round,pad=0.3,rounding_size=0.9",
                            linewidth=1.2, edgecolor="#7b1fa2", facecolor="#fafafa", zorder=2))
ax.text(IX + IW / 2, IY + IH - 1.4, "GT911 capacitive touch over I2C (Wire)",
        ha="center", va="center", fontsize=9, fontweight="bold", color="#7b1fa2", zorder=3)
ax.text(IX + IW / 2, IY + 3.0, "R4 SDA = A4  \N{MIDDLE DOT}  R4 SCL = A5  \N{MIDDLE DOT}  default addr 0x5D  \N{MIDDLE DOT}  INT/RST optional",
        ha="center", va="center", fontsize=8.2, color="#455a64", zorder=3)

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
