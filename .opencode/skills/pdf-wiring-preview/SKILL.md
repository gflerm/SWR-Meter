---
name: pdf-wiring-preview
description: Generate and visually verify the AA-30.ZERO / Uno R4 / display wiring diagram PDF. Use when the user asks to create, update, or review docs/display_wiring.pdf, or wants a PDF rendered to PNG for visual inspection. Provides an ASCII ladder of the exact wiring nets and a PyMuPDF render+verify step.
---

# Wiring-diagram PDF: generate & preview

Regenerates `docs/display_wiring.pdf` (Uno R4 Minima -> LEVEL-8P converter -> HU1-HU8
adapter -> Waveshare 2.4" ILI9341 SPI LCD) and verifies the result by rendering back
to a PNG.

## Prerequisites

- matplotlib (already used by `python/graph_results.py`)
- PyMuPDF, installed once: `python -m pip install pymupdf`

## Steps

### 1. Regenerate the diagram

```sh
python python/make_wiring_pdf.py
```

It writes `docs/display_wiring.pdf` and `docs/wiring_preview.png`.

### 2. Verify (render PDF -> PNG, check it is non-empty / multiple pages)

```sh
python -c "import pymupdf; d=pymupdf.open('docs/display_wiring.pdf'); print('pages', d.page_count); p=d[0].get_pixmap(dpi=110); p.save('docs/wiring_preview.png'); print('preview ok', p.width, p.height)"
```

Then `read` the generated PNG to confirm the layout looks right before declaring success.

## Wiring nets (single source of truth)

Signal lanes (R4 -> converter `B` -> `A` -> HU1-HU8 -> LCD):

| R4 pin         | Converter | HU | LCD  |
|----------------|-----------|----|------|
| D8  RST  (RST) | B5/A5     | 5  | RST  |
| D9  DC   (DC)  | B4/A4     | 4  | DC   |
| D10 CS   (CS)  | B3/A3     | 3  | CS   |
| D13 CLK  (CLK) | B2/A2     | 2  | CLK  |
| D11 DIN  (DIN) | B1/A1     | 1  | DIN  |

Power (NOT level-shifted — wired directly):

| Net          | R4    | Converter | HU  | LCD  |
|--------------|-------|-----------|-----|------|
| VCC 3.3 V    | 3.3V  | LV        | VCC | VCC  |
| 5 V / HV     | 5V    | HV        | 5V  | BL   |
| GND          | GND   | GND       | GND | GND  |

## Buttons (wired in the bottom panel of the PDF)

| Button | R4 pin | Function |
|--------|--------|----------|
| START  | D2     | Start a scan of the current band |
| BAND   | D3     | Cycle through the HF bands |
| MODE   | D4     | Toggle display layout (curve / numeric) |
| CAL    | D5     | Run a one-off calibration scan |

All are INPUT_PULLUP (pressed = LOW), debounced ~25 ms in `src/main.cpp`.

## Key constraints to preserve

- AA-30.ZERO uses D0/D1 (Serial1). Buttons: START=D2, BAND=D3, MODE=D4, CAL=D5.
  Display occupies D8-D13, so there is **no overlap**.
- Only DIN/CLK/CS/DC/RST are level-shifted (LEVEL-8P is a MOSFET pass-gate; each channel
  has a 10K pull-up to LV and HV). VCC(3.3 V), GND and BL(3.3 V) are wired directly.
- Keep the LV-side (3.3 V) wires short (<5 cm) to avoid TXS0108E overshoot.
- Firmware (`src/main.cpp`, PlatformIO + Arduino) drives the ILI9341 via
  Adafruit_GFX + Adafruit_ILI9341: CS=D10, DC=D9, RST=D8, with SPI MOSI=D11, SCK=D13.
  Buttons START/BAND/MODE/CAL are read on D2/D3/D4/D5 and debounced. Only DIN/CLK/CS/DC/RST
  are level-shifted.
- HU1-HU8's `TX/RX/GND` silkscreen labels are NOT used for this SPI hook-up; the board is
  a pass-through breakout. If the user supplies the real HU1-HU8 pin mapping, update both
  the diagram and this table.

