---
name: pdf-wiring-preview
description: Generate and visually verify the AA-30.ZERO / Uno R4 / DFRobot DFR0669 wiring diagram PDF. Use when the user asks to create, update, or review docs/display_wiring.pdf, or wants a PDF rendered to PNG for visual inspection. Provides an ASCII ladder of the exact wiring nets and a PyMuPDF render+verify step.
---

# Wiring-diagram PDF: generate & preview

Regenerates `docs/display_wiring.pdf` (Uno R4 Minima -> DFRobot DFR0669 3.5"
ILI9488 TFT + GT911 touch) and verifies the result by rendering back to a PNG.

## Prerequisites

- matplotlib (already used by `python/graph_results.py`)
- PyMuPDF, installed once: `python -m pip install pymupdf`

## Steps

### 1. Regenerate the diagram

```sh
python python/make_wiring_pdf.py
```

It writes `docs/display_wiring.pdf` and `docs/wiring_preview.png`.

### 2. Verify (render PDF -> PNG, check it is non-empty / one page)

```sh
python -c "import pymupdf; d=pymupdf.open('docs/display_wiring.pdf'); print('pages', d.page_count); p=d[0].get_pixmap(dpi=110); p.save('docs/wiring_preview.png'); print('preview ok', p.width, p.height)"
```

Then `read` the generated PNG to confirm the layout looks right before declaring success.

## Wiring nets (single source of truth)

The DFR0669 runs at 3.3-5.5 V, so **no level shifter / adapter board** is needed.
SPI is wired directly to the R4; GT911 touch and the LiPower fuel gauge are on I2C.

| DFR0669 pin | R4 pin        | Net                      |
|-------------|---------------|--------------------------|
| VCC         | 3.3-5.5 V     | power (direct)           |
| GND         | GND           | ground (direct)          |
| SCLK        | D13 (SPI SCK) | SPI clock                |
| MOSI        | D11 (SPI COPI)| SPI data out             |
| CS          | D10           | chip select              |
| DC          | D9            | data/command             |
| RES         | D8            | reset                    |
| BL          | on by default | backlight (no GPIO)      |
| SDA         | A4 (Wire SDA) | GT911 touch + MAX17043 gauge (I2C) |
| SCL         | A5 (Wire SCL) | GT911 touch + MAX17043 gauge (I2C) |
| MISO        | (optional)    | not used                 |
| INT         | (optional)    | touch interrupt, not used|
| SDCS        | (optional)    | MicroSD, not used        |

## LiPower Shield 0.5A (power + fuel gauge)

| Net | R4 pin | Details |
|-----|--------|---------|
| 5 V out | 5 V rail | 3.7 V LiPo boosted to 5 V (powers R4 + DFR0669) |
| MAX17043 gauge | I2C A4/A5 (0x36) | shares the touch I2C bus (no address conflict with GT911 @0x5D) |
| Low-batt ALRT | D2 | active-low alert at/below the set threshold (32 %) |
| Charge | mini-USB | charges the LiPo at 500 mA |

## Touch UI

The GT911 capacitive touch is the only control. Lower half of the screen is a row
of four touch buttons (**BAND | START | MODE | CAL**); upper half = "anywhere".

## Key constraints to preserve

- AA-30.ZERO uses D0/D1 (Serial1). Display uses D8-D13; touch uses A4/A5 — **no overlap**.
- Module is 3.3-5.5 V: SPI logic needs **no level shifter** (the earlier 2.4"
  ILI9341 + LEVEL-8P + HU1-HU8 setup is removed).
- GT911 default I2C address = 0x5D; INT/RST are optional.
- Firmware (`src/main.cpp`, PlatformIO + Arduino) drives the DFR0669 via
  DFRobot_GDL: `DFRobot_ILI9488_320x480_HW_SPI tft(DC=9, CS=10, RST=8, BL=NC)`,
  rotated to 480x320 landscape; `DFRobot_Touch_GT911 touch(addr=0x5D)` on Wire.
