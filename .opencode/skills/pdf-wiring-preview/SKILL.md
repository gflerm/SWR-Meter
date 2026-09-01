---
name: pdf-wiring-preview
description: Generate and visually verify the AA-30.ZERO / Uno R4 / LCDWIKI MSP4021 wiring diagram PDF. Use when the user asks to create, update, or review docs/display_wiring.pdf, or wants a PDF rendered to PNG for visual inspection. Provides an ASCII ladder of the exact wiring nets and a PyMuPDF render+verify step.
---

# Wiring-diagram PDF: generate & preview

Regenerates `docs/display_wiring.pdf` (Uno R4 Minima -> LCDWIKI MSP4021 4.0"
ST7796S TFT + XPT2046 touch) and verifies the result by rendering back to a PNG.

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

The MSP4021 (ST7796S) is 4-wire SPI; the XPT2046 touch shares the same SPI bus
(SJ1-SJ3 jumpered). A4/A5 are kept free for the battery gauge I2C (the vendor
UNO map uses A4/A5 for the display, which are moved to D8/D10 on the R4).

| MSP4021 pin | R4 pin        | Net                       |
|-------------|---------------|---------------------------|
| VCC         | 5 V           | power (direct)            |
| GND         | GND           | ground (direct)           |
| SCK         | D13 (SPI SCK) | SPI clock                 |
| SDI(MOSI)   | D11 (SPI COPI)| SPI data out              |
| SDO(MISO)   | D12 (SPI CIPO)| SPI data in (optional)    |
| CS          | D10           | chip select               |
| DC/RS       | D9            | data/command              |
| RESET       | D8            | reset                     |
| LED         | D7            | backlight (high = on)     |
| T_CS        | D6            | XPT2046 chip select       |
| T_IRQ       | A0            | XPT2046 interrupt         |

## LiPower Shield 0.5A (power + fuel gauge)

| Net | R4 pin | Details |
|-----|--------|---------|
| 5 V out | 5 V rail | 3.7 V LiPo boosted to 5 V (powers R4 + MSP4021) |
| MAX17043 gauge | I2C A4/A5 (0x36) | battery voltage + state-of-charge |
| Low-batt ALRT | D2 | active-low alert at/below the set threshold (32 %) |
| Charge | mini-USB | charges the LiPo at 500 mA |

## Touch UI

The XPT2046 resistive touch is the only control. Lower half of the screen is a row
of four touch buttons (**BAND | START | MODE | CAL**); upper half = "anywhere".

## Key constraints to preserve

- AA-30.ZERO uses D0/D1 (Serial1). Display/touch use D6-D13 (SPI) + A0; battery
  I2C uses A4/A5; low-batt ALRT uses D2 — **no overlap**.
- Module is 3.3-5 V: run the display/touch at 3.3 V logic (or add a level shifter
  if driving from 5 V logic) per the MSP4021 manual.
- Firmware (`src/main.cpp`, PlatformIO + Arduino) drives the MSP4021 via
  **TFT_eSPI** (ST7796S + XPT2046); the pin map is set in `platformio.ini` build_flags.
