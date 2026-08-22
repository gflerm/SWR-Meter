"""Render the Uno R4 / AA-30.ZERO / display wiring diagram to docs/display_wiring.pdf."""
import os
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
C_SIGLV = "#7b1fa2"
C_RES = "#8d6e63"

fig, ax = plt.subplots(figsize=(16, 14), dpi=150)
ax.set_xlim(0, 100)
ax.set_ylim(0, 122)
ax.axis("off")
LANES = [("RST", 64), ("DC", 57), ("CS", 50), ("CLK", 43), ("DIN", 36)]
Y_33V, Y_5V, Y_GND = 30, 25, 20
BOX_TOP, BOX_BOT = 70, 18

BLOCK_X = {"r4": (2.0, 16.0), "lv": (22.0, 17.0), "hu": (47.0, 13.0), "lcd": (67.0, 16.5)}
R4_R = BLOCK_X["r4"][0] + BLOCK_X["r4"][1]      # 18
L8_L = BLOCK_X["lv"][0]                          # 22
L8_R = BLOCK_X["lv"][0] + BLOCK_X["lv"][1]       # 39
HU_L = BLOCK_X["hu"][0]                          # 47
HU_R = BLOCK_X["hu"][0] + BLOCK_X["hu"][1]       # 60
LCD_L = BLOCK_X["lcd"][0]                        # 67


def block(key, title, sub, ec):
    x, w = BLOCK_X[key]
    ax.add_patch(FancyBboxPatch((x, BOX_BOT), w, BOX_TOP - BOX_BOT,
                                boxstyle="round,pad=0.3,rounding_size=1.1",
                                linewidth=1.6, edgecolor=ec, facecolor="white", zorder=2))
    ax.text(x + w / 2, BOX_TOP - 2.2, title, ha="center", va="center",
            fontsize=13, fontweight="bold", color="#263238", zorder=4)
    ax.text(x + w / 2, BOX_TOP - 5.6, sub, ha="center", va="center",
            fontsize=8.4, color="#455a64", zorder=4)


block("r4", "Uno R4 Minima", "Renesas RA4M1  |  5 V", "#78909c")
block("lv", "LEVEL-8P", "8-ch converter  |  HV=5V  LV=3.3V", "#5d4037")
block("hu", "HU1-HU8", "adapter board\n(pass-through)", "#45545f")
block("lcd", 'Waveshare 2.4" LCD', "ILI9341 | 240x320\n3.3 V SPI", "#00695c")


def wire(x1, y1, x2, y2, color, lw=1.7, z=3):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="->", mutation_scale=10,
                                 linewidth=lw, color=color, shrinkA=0, shrinkB=0, zorder=z))


def tag(x, y, text, color, ha="center", fs=8.0, weight="bold"):
    ax.text(x, y, text, fontsize=fs, color=color, fontweight=weight, ha=ha, va="center",
            zorder=7, bbox=dict(boxstyle="round,pad=0.13", fc="white", ec="none"))


# --- signal lanes ---------------------------------------------------------
for i, (name, y) in enumerate(LANES):
    ch = i + 1
    wire(R4_R, y, L8_L, y, C_SIG, 1.7)
    tag((R4_R + L8_L) / 2, y + 1.7, name, C_SIG)
    wire(L8_L, y, L8_R, y, C_SIGLV, 1.7)
    wire(L8_R, y, HU_L, y, C_SIGLV, 1.7)
    tag((L8_R + HU_L) / 2, y + 1.7, name, C_SIGLV)
    wire(HU_R, y, LCD_L, y, C_SIGLV, 1.7)
    tag((HU_R + LCD_L) / 2, y + 1.7, name, C_SIGLV)
    tag(L8_L + 1.3, y, f"B{ch}", C_SIG, ha="left", fs=8.3)
    tag(L8_R - 1.3, y, f"A{ch}", C_SIGLV, ha="right", fs=8.3)
    tag(HU_L + 1.3, y, f"{ch}", C_SIGLV, ha="left", fs=8.3)
    tag(HU_R - 1.3, y, f"{ch}", C_SIGLV, ha="right", fs=8.3)
    tag(LCD_L + 1.3, y, name, C_SIGLV, ha="left", fs=8.3)

r4_pin = {64: "D8  RST", 57: "D9  DC", 50: "D10  CS", 43: "D13  CLK", 36: "D11  DIN"}
for y, txt in r4_pin.items():
    tag(R4_R - 0.6, y, txt, C_SIG, ha="right", fs=8.2)

# --- power nets ------------------------------------------------------------
for y, label, color in [(Y_33V, "VCC 3.3 V", C_33), (Y_5V, "5 V (HV)", C_U5V), (Y_GND, "GND", C_GND)]:
    wire(R4_R, y, L8_L, y, color, 2.2)
    tag((R4_R + L8_L) / 2, y - 2.2, label, color)
    wire(L8_L, y, L8_R, y, color, 2.2)
    wire(L8_R, y, HU_L, y, color, 2.2)
    wire(HU_R, y, LCD_L, y, color, 2.2)
    tag((HU_R + LCD_L) / 2, y - 2.2, label, color)

tag(L8_L + 1.3, Y_33V, "LV", C_33, ha="left")
tag(L8_R - 1.3, Y_33V, "LV", C_33, ha="right")
tag(L8_L + 1.3, Y_5V, "HV", C_U5V, ha="left")
tag(L8_R - 1.3, Y_5V, "HV", C_U5V, ha="right")
tag(L8_L + 1.3, Y_GND, "GND", C_GND, ha="left")
tag(L8_R - 1.3, Y_GND, "GND", C_GND, ha="right")
tag(HU_L + 1.3, Y_33V, "VCC", C_33, ha="left")
tag(HU_R - 1.3, Y_33V, "VCC", C_33, ha="right")
tag(HU_L + 1.3, Y_5V, "5V", C_U5V, ha="left")
tag(HU_R - 1.3, Y_5V, "5V", C_U5V, ha="right")
tag(HU_L + 1.3, Y_GND, "GND", C_GND, ha="left")
tag(HU_R - 1.3, Y_GND, "GND", C_GND, ha="right")
tag(LCD_L + 1.3, Y_33V, "VCC", C_33, ha="left")
tag(LCD_L + 1.3, Y_5V, "BL", C_U5V, ha="left")
tag(LCD_L + 1.3, Y_GND, "GND", C_GND, ha="left")

# --- inset panel: one converter channel (MOSFET pass-gate) ---------------
IX, IY, IW, IH = 22, 76, 39, 13
ax.add_patch(FancyBboxPatch((IX, IY), IW, IH,
                            boxstyle="round,pad=0.3,rounding_size=1.0",
                            linewidth=1.2, edgecolor="#5d4037", facecolor="#fafafa", zorder=2))
ax.text(IX + IW / 2, IY + IH - 1.6, "One LEVEL-8P channel (internal)",
        ha="center", va="center", fontsize=9, fontweight="bold", color="#5d4037", zorder=3)

ccx, ccy = IX + IW / 2, IY + 5.0
R = 7.5
# LV / HV rails + pull-ups
wire(ccx - R, ccy + 3.6, ccx - R, ccy - 3.0, C_33, 1.2)
wire(ccx + R, ccy + 3.6, ccx + R, ccy - 3.0, C_U5V, 1.2)
ax.plot([ccx - R, ccx - R], [ccy - 1.6, ccy + 1.4], color=C_RES, lw=1.5, zorder=3)
ax.plot([ccx + R, ccx + R], [ccy - 1.6, ccy + 1.4], color=C_RES, lw=1.5, zorder=3)
ax.text(ccx - R, ccy + 4.3, "LV", ha="center", va="bottom", fontsize=8.5, color=C_33, fontweight="bold")
ax.text(ccx + R, ccy + 4.3, "HV", ha="center", va="bottom", fontsize=8.5, color=C_U5V, fontweight="bold")
ax.text(ccx - R, ccy - 3.4, "10K", ha="center", va="top", fontsize=7, color=C_RES)
ax.text(ccx + R, ccy - 3.4, "10K", ha="center", va="top", fontsize=7, color=C_RES)
# gate rail
wire(ccx - R, ccy + 3.4, ccx + R, ccy + 3.4, C_GND, 1.2)
wire(ccx, ccy + 3.4, ccx, ccy + 1.2, C_GND, 1.2)
ax.add_patch(FancyBboxPatch((ccx - 1.7, ccy + 1.2), 3.4, 2.4,
                            boxstyle="round,pad=0.1,rounding_size=0.3",
                            linewidth=1.1, edgecolor="#263238", facecolor="white", zorder=3))
ax.text(ccx, ccy + 2.4, "FET", ha="center", va="center", fontsize=6.8, color="#263238", zorder=4)
# source/drain down
wire(ccx - 3.5, ccy + 1.2, ccx - 3.5, ccy - 3.0, C_SIGLV, 1.2)
wire(ccx + 3.5, ccy + 1.2, ccx + 3.5, ccy - 3.0, C_SIGLV, 1.2)
# signal line
wire(ccx - 11, ccy - 3.0, ccx + 11, ccy - 3.0, C_SIGLV, 1.5)
ax.text(ccx - 11, ccy - 4.2, "A  (LV1\N{EN DASH}8)", ha="left", va="top", fontsize=7.5, color=C_SIGLV)
ax.text(ccx + 11, ccy - 4.2, "B  (HV1\N{EN DASH}8)", ha="right", va="top", fontsize=7.5, color=C_U5V)

# --- heading ----------------------------------------------------------
ax.text(2.0, 116.0, "Uno R4 Minima \N{RIGHTWARDS ARROW} Waveshare 2.4\" SPI LCD  \N{MIDDLE DOT}  logic-level converter wiring",
        ha="left", va="center", fontsize=13.5, fontweight="bold", color="#263238")
ax.text(2.0, 113.0, "Waveshare 2.4\" SPI LCD (ILI9341, 240x320, 65K)  \N{MIDDLE DOT}  LEVEL-8P / TXS0108E converter  \N{MIDDLE DOT}  HU1-HU8 adapter",
        ha="left", va="center", fontsize=9, color="#455a64")

# --- footnotes (clear band under the subtitle, above the inset) ---------
ax.text(2.0, 109.0, "AA-30.ZERO:  UART1 TX \N{RIGHTWARDS ARROW} R4 D0 (Serial1 RX)  \N{MIDDLE DOT}  UART1 RX \N{RIGHTWARDS ARROW} R4 D1 (Serial1 TX)  \N{MIDDLE DOT}  GND common.",
        ha="left", va="center", fontsize=8.2, color="#37474f")
ax.text(2.0, 106.0, "DIN/CLK/CS/DC/RST are level-shifted; VCC (3.3 V), GND and BL are direct. Display uses D8\N{EN DASH}D13, buttons D2\N{EN DASH}D5 \N{EM DASH} no overlap with AA-30 (D0/D1). Keep LV wires <5 cm.",
        ha="left", va="center", fontsize=8.0, color="#37474f")

# --- legend (in the clear strip left of the inset panel) --------------
handles = [
    plt.Line2D([0], [0], color=C_SIG, lw=1.8, label="Logic (5 V R4 side)"),
    plt.Line2D([0], [0], color=C_SIGLV, lw=1.8, label="Logic (3.3 V display side)"),
    plt.Line2D([0], [0], color=C_U5V, lw=2.2, label="5 V"),
    plt.Line2D([0], [0], color=C_33, lw=2.2, label="3.3 V"),
    plt.Line2D([0], [0], color=C_GND, lw=2.2, label="GND"),
]
ax.legend(handles=handles, loc="center left", bbox_to_anchor=(0.005, 0.78),
          ncol=2, fontsize=8, frameon=False)

# --- bottom panel: buttons wiring --------------------------------------
# Button row occupies the band below the blocks (blocks end at BOX_BOT=18).
BY = 9.0                        # y of the wire/switch row
BX = [4, 12, 20, 28]            # x of each button switch
BTN_LBL = ["START", "BAND", "MODE", "CAL"]
BTN_PIN = ["D2", "D3", "D4", "D5"]
BTN_R4X = [52, 60, 68, 76]      # x where each button's net meets the "Uno Dx" label

# common ground rail (buttons share one end to GND)
ax.plot([2, BX[0] + 0.95], [BY - 1.2, BY - 1.2], color=C_GND, lw=1.2, zorder=3)
ax.text(2.4, BY - 0.7, "GND", ha="left", va="center", fontsize=7, color=C_GND, fontweight="bold")

for i, (lbl, pin, bx, rx) in enumerate(zip(BTN_LBL, BTN_PIN, BX, BTN_R4X)):
    # switch symbol
    ax.add_patch(FancyBboxPatch((bx, BY - 1.1), 1.7, 1.5,
                                boxstyle="round,pad=0.08,rounding_size=0.2",
                                linewidth=1.1, edgecolor="#263238", facecolor="white", zorder=3))
    ax.add_patch(FancyArrowPatch((bx + 0.85, BY - 1.1), (bx + 0.85, BY + 1.0),
                                 arrowstyle="-|>", mutation_scale=6, linewidth=1.1,
                                 color="#263238", shrinkA=0, shrinkB=0, zorder=3))
    ax.text(bx + 0.85, BY + 1.4, lbl, ha="center", va="bottom", fontsize=7.2,
            fontweight="bold", color="#263238", zorder=4)
    # net to GND rail
    ax.plot([bx + 0.85, bx + 0.85], [BY - 1.1, BY - 1.2], color=C_GND, lw=1.2, zorder=3)
    # signal net down to the R4 pin label, then across to its x
    ax.add_patch(FancyArrowPatch((bx + 0.85, BY - 1.1), (bx + 0.85, BY - 3.2),
                                 arrowstyle="->", mutation_scale=9, linewidth=1.5,
                                 color=C_SIG, shrinkA=0, shrinkB=0, zorder=3))
    ax.plot([bx + 0.85, rx], [BY - 3.2, BY - 3.2], color=C_SIG, lw=1.5, zorder=3)
    ax.add_patch(FancyArrowPatch((rx, BY - 3.2), (rx, BY - 1.2),
                                 arrowstyle="->", mutation_scale=9, linewidth=1.5,
                                 color=C_SIG, shrinkA=0, shrinkB=0, zorder=3))
    ax.text(rx, BY - 0.6, f"Uno {pin}", ha="center", va="bottom", fontsize=7.6,
            color=C_SIG, fontweight="bold", zorder=4)

# panel frame + title
ax.add_patch(FancyBboxPatch((1.0, 13.0), 84.0, 4.2,
                            boxstyle="round,pad=0.3,rounding_size=0.9",
                            linewidth=1.2, edgecolor="#45545f", facecolor="white", zorder=2))
ax.text(2.4, 13.6, "Buttons (INPUT_PULLUP, active-low) \N{RIGHTWARDS ARROW} Uno R4", ha="left", va="center",
        fontsize=9, fontweight="bold", color="#45545f", zorder=4)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
# Write to a temp file first, then replace, so the target is never half-written
# and a lock (e.g. an open PDF viewer) is the only thing that can fail.
import time
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
