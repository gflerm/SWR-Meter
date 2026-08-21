"""Render SWR vs Frequency graphs from result_data.json.

Produces:
  - One PDF per band in graphs/<band>.pdf
  - One combined multi-page PDF: graphs/AA-30_ZERO_all_bands.pdf
Formatted with titled heads, axis units, SWR reference lines and a footer.
"""
import json, os, sys, datetime, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.backends.backend_pdf import PdfPages

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON = os.path.join(BASE, "result_data.json")
OUT_DIR = os.path.join(BASE, "graphs")
os.makedirs(OUT_DIR, exist_ok=True)

LOAD_NOTE = "Reference load: 50 \u03a9 | Points per band: 100 | IARU Region 1 bands"
GENERATED = datetime.date.today().isoformat()

try:
    with open(JSON, encoding="utf-8") as fh:
        DATA = json.load(fh)
except FileNotFoundError:
    sys.exit(f"Missing {JSON} - run python/sweep_bands.py first.")
except json.JSONDecodeError as exc:
    sys.exit(f"Malformed {JSON}: {exc}")

plt.rcParams.update({
    "font.size": 10,
    "axes.titlesize": 14,
    "axes.titleweight": "bold",
    "axes.labelsize": 11,
    "figure.facecolor": "white",
    "savefig.facecolor": "white",
    "axes.grid": True,
    "grid.alpha": 0.3,
})

def make_figure(name, meta, title_kind="page"):
    pts = meta["points"]
    freq = [p["f"] for p in pts]
    swr = [p["swr"] for p in pts]
    lo, hi = meta["low"] / 1e6, meta["high"] / 1e6

    fig, ax = plt.subplots(figsize=(9.5, 5.5), dpi=150)

    # --- heading block ---
    fig.text(0.08, 0.93, "RigExpert AA-30.ZERO \u2014 Antenna Analyzer Sweep",
             fontsize=16, fontweight="bold", va="center")
    fig.text(0.08, 0.885, f"{name} Band  \u00b7  {lo:.3f} \u2013 {hi:.3f} MHz",
             fontsize=13, color="#1f4f8f", va="center", fontweight="bold")

    ax.plot(freq, swr, "-", color="#1f77b4", lw=1.7, label="SWR")
    ax.plot(freq, swr, "o", color="#1f77b4", ms=2.4)
    ax.axhline(1.0, color="#2ca02c", ls="--", lw=1.1, label="SWR 1.00 (perfect)")
    ax.axhline(2.0, color="#d62728", ls=":", lw=1.1, label="SWR 2.00")
    ax.axhline(3.0, color="#ff7f0e", ls=":", lw=0.9, alpha=0.7, label="SWR 3.00")

    lo_mhz, hi_mhz = lo, hi
    ax.set_xlim(lo_mhz - 0.02 * (hi_mhz - lo_mhz), hi_mhz + 0.02 * (hi_mhz - lo_mhz))
    ax.set_ylim(bottom=0.9)
    ax.set_xlabel("Frequency (MHz)", fontsize=11, fontweight="bold")
    ax.set_ylabel("SWR  (50 \u03a9 system)", fontsize=11, fontweight="bold")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="upper left", fontsize=8, framealpha=0.9)

    swr_min = min(swr)
    f_at_min = freq[swr.index(swr_min)]
    ax.annotate(f"Minimum SWR  {swr_min:.3f}  @  {f_at_min:.3f} MHz",
                xy=(f_at_min, swr_min), xytext=(10, 22), textcoords="offset points",
                fontsize=9, color="#d62728", fontweight="bold",
                arrowprops=dict(arrowstyle="->", color="#d62728", lw=1.4))

    # --- footer ---
    fig.text(0.08, 0.045, f"{LOAD_NOTE}  |  Generated {GENERATED}",
             fontsize=8, color="#555555", va="center")
    fig.subplots_adjust(left=0.09, right=0.97, top=0.80, bottom=0.14)
    return fig, swr_min, f_at_min

def make_cover():
    """A clean title page: heading + a band summary table (Band/Range/Min SWR)."""
    cover = plt.figure(figsize=(11, 8.5), dpi=150)

    cover.text(0.5, 0.90, "RigExpert AA-30.ZERO", fontsize=32,
               fontweight="bold", ha="center", color="#1f4f8f")
    cover.text(0.5, 0.83, "Antenna Analyzer \u2014 Full Band Sweep", fontsize=19, ha="center")
    cover.text(0.5, 0.77, "SWR vs Frequency  \u00b7  160 m \u2192 10 m  \u00b7  50 \u03a9 reference load",
               fontsize=12, ha="center", color="#333333")

    cover.add_artist(Line2D([0.15, 0.85], [0.735, 0.735], color="#cccccc", lw=1.2, transform=cover.transFigure))

    # Table header
    hx = {"band": 0.20, "range": 0.40, "swr": 0.68, "freq": 0.82}
    cover.text(hx["band"], 0.685, "Band", fontsize=11, fontweight="bold", color="#1f4f8f")
    cover.text(hx["range"], 0.685, "Frequency Range (MHz)", fontsize=11, fontweight="bold", color="#1f4f8f")
    cover.text(hx["swr"], 0.685, "Min SWR", fontsize=11, fontweight="bold", color="#1f4f8f")
    cover.text(hx["freq"], 0.685, "at (MHz)", fontsize=11, fontweight="bold", color="#1f4f8f")
    cover.add_artist(Line2D([0.15, 0.85], [0.66, 0.66], color="#1f4f8f", lw=1.0, transform=cover.transFigure))

    y = 0.625
    for name, meta in DATA.items():
        pts = meta["points"]
        lo, hi = meta["low"] / 1e6, meta["high"] / 1e6
        swr_min = min(p["swr"] for p in pts) if pts else 0.0
        f_min = pts[[p["swr"] for p in pts].index(swr_min)]["f"] if pts else 0.0

        cover.text(hx["band"], y, f"{name}", fontsize=11, fontweight="bold")
        cover.text(hx["range"], y, f"{lo:.3f} \u2013 {hi:.3f}", fontsize=11)
        cover.text(hx["swr"], y, f"{swr_min:.2f}", fontsize=11, ha="center")
        cover.text(hx["freq"], y, f"{f_min:.3f}", fontsize=11, ha="center")
        y -= 0.047

    cover.add_artist(Line2D([0.15, 0.85], [y + 0.012, y + 0.012], color="#cccccc", lw=1.0, transform=cover.transFigure))
    cover.text(0.5, 0.10, LOAD_NOTE, fontsize=10, ha="center", color="#333333")
    cover.text(0.5, 0.06, f"Generated {GENERATED}", fontsize=9, ha="center", color="#777777")
    return cover

for name, meta in DATA.items():
    if not meta["points"]:
        continue
    fig, swr_min, f_at_min = make_figure(name, meta)
    out = os.path.join(OUT_DIR, f"{name}.pdf")
    fig.savefig(out, format="pdf")
    plt.close(fig)
    print(f"{name}: min SWR {swr_min:.3f} @ {f_at_min:.3f} MHz -> {out}")

# Combined multi-page PDF with cover + one page per band
combined = os.path.join(OUT_DIR, "AA-30_ZERO_all_bands.pdf")
with PdfPages(combined) as pdf:
    pdf.savefig(make_cover())
    for name, meta in DATA.items():
        if not meta["points"]:
            continue
        fig, swr_min, f_at_min = make_figure(name, meta)
        pdf.savefig(fig)
        plt.close(fig)
print("Combined PDF:", combined)
print("Done.")
