---
title: Uno R4 Minima + RigExpert AA-30.ZERO SWR Meter
description: Arduino Uno R4 Minima bridge to a RigExpert AA-30.ZERO antenna analyzer measuring SWR, R and X across the HF amateur bands, with a 4.0-inch LCDWIKI MSP4021 ST7796S TFT + XPT2046 touch readout.
status: on_hold
priority: medium
startDate: "2026-08-27"
tags: [arduino, uno-r4, swr, rf, antenna, dfr0669, hardware]
goals:
  - title: "Communication verified with AA-30.ZERO (ver/fq/sw/frx)"
    done: true
  - title: "Accurate readings confirmed against a 50-ohm reference load"
    done: true
  - title: "Full 160 m - 10 m Region 1 sweep recorded and graphed"
    done: true
  - title: "4.0-inch MSP4021 ST7796S TFT live SWR curve + numeric readout"
    done: true
  - title: "XPT2046 resistive-touch UI (START/BAND/MODE/CAL) wired and working"
    done: true
  - title: "Calibration routine and band presets polished for real use"
    done: false
reminders:
  - due: "2026-09-30"
    note: "Field-test calibration routine on a real antenna"
    done: false
timeline:
  - title: "AA-30.ZERO serial protocol bridge working"
    date: "2026-08-27"
    done: true
  - title: "Reference-load accuracy validation"
    date: "2026-08-27"
    done: true
  - title: "Sweep graphing + display readout"
    date: "2026-08-28"
    done: true
  - title: "Calibration + presets polish"
    date: "2026-09-30"
    done: false
---

# Uno R4 Minima / RigExpert AA-30.ZERO SWR Meter

A bridge between an **Arduino Uno R4 Minima** and a **RigExpert AA-30.ZERO** antenna/cable
analyzer to measure and display Standing Wave Ratio (SWR), Resistance (R) and Reactance (X)
across the HF amateur bands (160 m → 10 m). The AA-30.ZERO works as an Arduino shield
(UART @ 38400 baud) and is a 5 V device.

![RigExpert AA-30.ZERO board assembly](media/aa30_zero_assembly.jpeg)

## ✅ Status

- **Communication verified working** (`ver` → `AA-30.ZERO 200`, `fq`/`sw` → `OK`, `frx` → `freq,R,X` stream).
- Accurate measurement confirmed with a **50 Ω reference load** across all bands (R ≈ 50 Ω, SWR ≈ 1.0).
- Data validation guard in place to reject bogus readings (NaN/Inf, `R`/`|X|` > 1 MΩ, SWR out of range).
- Full 160 m → 10 m sweep recorded and graphed (IARU **Region 1** bands, 100 points/band).
- 🖥️ **Display**: LCDWIKI MSP4021 4.0" ST7796S TFT (480×320, 3.3–5 V), XPT2046 resistive touch (SPI). Driven by TFT_eSPI.
- 🎛️ **Controls + UI**: XPT2046 resistive touchscreen (START/BAND/MODE/CAL zones) + live SWR curve / numeric readout.
- 🔋 **Power**: LiPower Shield 0.5A (3.7 V LiPo → 5 V) with MAX17043 fuel gauge — battery % shown on the display, low-batt alert on D2. Read the battery with the shield switch ON (the gauge reads ~1 V when the load is switched off).

## 🛠️ Hardware

| Component | Role |
|-----------|------|
| **Uno R4 Minima** (Renesas RA4M1) | Main controller |
| **AA-30.ZERO** | RF analyzer, 0.06–30 MHz, UART @ 38400 |
| **LCDWIKI MSP4021 4.0" TFT** (ST7796S, 480×320) | Display + XPT2046 resistive touch (TFT_eSPI) |
| **LiPower Shield 0.5A** (ACS33721L) | 3.7 V LiPo → 5 V power + MAX17043 fuel gauge (I²C) |

### Wiring (AA-30.ZERO UART1 → Uno R4)
The AA-30.ZERO has two UARTs. On the R4, the **only reliable path** is hardware `Serial1`
(D0/D1), which connects to the analyzer's **UART1** interface:

```
AA-30.ZERO UART1 TX  ->  Uno D0  (Serial1 RX)
AA-30.ZERO UART1 RX  ->  Uno D1  (Serial1 TX)
AA-30.ZERO GND       ->  Uno GND (common ground required)
```

> ⚠️ The R4's `SoftwareSerial` cannot initialize on D4/D7 (the analyzer's default UART2),
> so UART1 + hardware `Serial1` is used. The analyzer must be set to **UART1**.

### Wiring (Display + Touch → Uno R4)
The LCDWIKI MSP4021 4.0" is a 4-wire SPI ST7796S module with an XPT2046 resistive
touch that shares the same SPI bus. SPI pins (MOSI/SCK/MISO) are fixed on the R4;
the display and touch control pins are remapped so they don't clash with the R4's
I²C pins (A4/A5, used by the battery gauge).

```
MSP4021 pin    Uno R4            Net
--------------  ---------------  --------------
VCC            5 V               power (direct)
GND            GND               ground (direct)
SCK            D13 (SPI SCK)     SPI clock
SDI(MOSI)      D11 (SPI COPI)    SPI data out
SDO(MISO)      D12 (SPI CIPO)    SPI data in (optional)
CS             D10               chip select
DC/RS          D9                data/command
RESET          D8                reset
LED            D7                backlight (high = on)
T_CS           D6                XPT2046 chip select
T_IRQ          A0                XPT2046 interrupt
```

> ✅ **Pin check** — the AA-30 uses D0/D1 (Serial1); the battery gauge I²C uses
> A4/A5; low-batt ALRT uses D2. The display/touch use D6–D13 (SPI) + A0, so there
> is **no overlap**. Touch shares MOSI/SCK/MISO with the display (SJ1-SJ3 jumpered).
> Note: the vendor's Arduino-UNO map uses A4 (RESET) / A5 (CS); on the R4 those are
> the I²C pins, so they are moved to D8/D10.

### Touch UI
The XPT2046 resistive touch is the only control. The lower half of the screen is a
row of four touch buttons (**BAND | START | MODE | CAL**); touching the upper half
is "anywhere" (advance/dismiss a screen).

## 📡 Communication Protocol

| Command | Description | Response |
|---------|-------------|----------|
| `ver` | Return analyzer type and firmware version. | `AA-30.ZERO XXX` |
| `fqXXXXXXXXXX` | Set center frequency (Hz). | `OK` |
| `swXXXXXXXXXX` | Set sweep range (Hz). | `OK` |
| `frxNNNN` | Perform NNNN steps (NNNN+1 points). | `freq,R,X\n` per point, then `OK` |

Commands and responses are ASCII on UART @ **38400 baud**. `ver` returns `AA-30.ZERO 200`; a
scan outputs CSV lines `frequency(MHz),R,X` ending in `OK`. Practical max ≈ **700 points** per sweep.

See [`docs/AA-30_ZERO_data_exchange_protocol.md`](docs/AA-30_ZERO_data_exchange_protocol.md).

## 💾 Software

The firmware is split into focused modules under `src/`, orchestrated by a thin
`main.cpp`:

| File | Responsibility |
|------|----------------|
| `main.cpp` | `setup()`, `loop()`, the UI state machine, PC-command handling |
| `config.h` | Pin map, constants, shared types / tables |
| `hardware.h`/`.cpp` | all global state + the `tft` display object (TFT_eSPI) + the AA-30 `Serial1` |
| `display.h`/`.cpp` | every MSP4021 (ST7796S) render call + external-control splash |
| `touch.h`/`.cpp` | XPT2046 resistive-touch scan + tap→action classifier |
| `battery.h`/`.cpp` | LiPower MAX17043 fuel gauge (voltage, SOC, low-batt alert) |
| `rigexpert.h`/`.cpp` | AA-30 UART poll, parser, scan driver |
| `calibration.h`/`.cpp` | calibration wizard, EEPROM table, correction |
| `telemetry.h`/`.cpp` | `@STATE`/`@BAND`/`@MODE`/`@CTRL`/`@CAL*` emit helpers |

The AA-30 `freq,R,X` stream is parsed (syntactically), validated for physical
plausibility on normal sweeps, and rendered to the MSP4021 as an SWR curve or a
numeric readout. A **CALIBRATE** wizard (tap CAL) sweeps every HF band × 20
points against a **50 Ω** reference, builds a per-band R/X offset table, stores
it to EEPROM, and applies the correction (with linear interpolation between
table points) to all normal sweeps. Progress (band + point + bar) is shown
during calibration. A host app can take over via `!CTRL:EXTERNAL` (display is
bypassed for speed) and return with `!CTRL:LOCAL`.

Built with **PlatformIO** + Arduino framework (requires **TFT_eSPI**,
declared automatically in `platformio.ini`).

- **`python/`** — `sweep_bands.py` (drive the analyzer + record results),
  `graph_results.py` (render PDF graphs with matplotlib), and
  `sim_display.py` (**Windows simulator GUI**: emulates the display and
  provides soft START/BAND/MODE/CAL controls that drive the unit over serial).

## 📊 Results & Graphs

Sweep results (IARU Region 1, 100 points/band, 50 Ω load):

| Band | R (Ω) | X (Ω) | Min SWR |
|------|-------|-------|---------|
| 160 m | ~50.4 | ~0 | 1.00 |
| 80 m | ~50.4 | ~0 | 1.01 |
| 60 m | ~50.4 | ~0 | 1.01 |
| 40 m | ~50.4 | ~0 | 1.01 |
| 30 m | ~50.4 | ~0 | 1.01 |
| 20 m | ~50.4 | ~0 | 1.01 |
| 17 m | ~50.4 | ~0 | 1.00 |
| 15 m | ~50.4 | ~0 | 1.02 |
| 12 m | ~50.4 | ~0 | 1.02 |
| 10 m | ~50.4 | ~0 | 1.02 |

- Full tables: [`result.md`](result.md) · raw data: [`result_data.json`](result_data.json)

### Graphs (SWR vs Frequency, PDF)
- [`graphs/160m.pdf`](graphs/160m.pdf) · [`80m.pdf`](graphs/80m.pdf) · [`60m.pdf`](graphs/60m.pdf) ·
  [`40m.pdf`](graphs/40m.pdf) · [`30m.pdf`](graphs/30m.pdf) · [`20m.pdf`](graphs/20m.pdf) ·
  [`17m.pdf`](graphs/17m.pdf) · [`15m.pdf`](graphs/15m.pdf) · [`12m.pdf`](graphs/12m.pdf) · [`10m.pdf`](graphs/10m.pdf)
- Combined, all bands in one PDF: [`graphs/AA-30_ZERO_all_bands.pdf`](graphs/AA-30_ZERO_all_bands.pdf)

## 🖼️ Media

See the assembly photo above. Demo video: [AA-30.ZERO demo](media/aa30_zero_demo.mp4)

## 📚 Documentation (`docs/`)

- [`AA-30_ZERO_schematics_and_drawings.pdf`](docs/AA-30_ZERO_schematics_and_drawings.pdf) — official schematics & BOM
- [`AA-30_ZERO_pcb_top.jpg`](docs/AA-30_ZERO_pcb_top.jpg) / [`AA-30_ZERO_pcb_bottom.jpg`](docs/AA-30_ZERO_pcb_bottom.jpg) — PCB drawings
- [`AA-30_ZERO_data_exchange_protocol.md`](docs/AA-30_ZERO_data_exchange_protocol.md) — command protocol
- [`AA-30_ZERO_getting_started.md`](docs/AA-30_ZERO_getting_started.md) — board overview & Arduino pairing
- `docs/display_wiring.pdf` — MSP4021 display + XPT2046 touch wiring reference (shared SPI)
- [`docs/firmware_reference.md`](docs/firmware_reference.md) — firmware functions, state machine & serial protocol
- [`docs/SPEC_SKELETON.md`](docs/SPEC_SKELETON.md) — reusable build-and-verify spec template + lessons learned
- [`tests/`](tests/) — deterministic firmware test suite ([`README.md`](tests/README.md), log)

## 🔨 Build / Upload

The firmware is built with **PlatformIO** (Arduino framework, board `uno_r4_minima`):

```sh
pio run                  # compile
pio run -t upload        # flash over USB/DFU
pio run -t monitor       # serial monitor @ 115200
```

Config: [`platformio.ini`](platformio.ini); source: `src/main.cpp`. The TFT_eSPI
library (configured for the ST7796S + XPT2046) is pulled automatically via `lib_deps`.

USB CDC console: 115200 baud. AA-30 UART: 38400 baud (`Serial1`).

### PC Simulator (test the UI without touching the screen)

`python/sim_display.py` shows the screen on your PC and adds soft
START/BAND/MODE/CAL controls that inject actions into the firmware over the USB
CDC serial port. Requires `pyserial` and `pillow`:

```sh
pip install pyserial pillow
python python/sim_display.py --port COMxx      # real hardware
python python/sim_display.py --mock            # offline demo (no serial)
```

The firmware emits machine-readable telemetry (`@STATE:`, `@BAND:`, `@POINT:`,
`@CAL*`, `@CTRL:`) that the simulator uses to render each screen exactly,
including the calibration progress (band + point + bar). Soft controls send
`!BTN:...` commands; `!CTRL:EXTERNAL`/`!CTRL:LOCAL` toggle host control; and
non-command serial lines still pass through to the AA-30 in IDLE.

## 🚀 Next Steps

1. ✅ **Full stack wired on real hardware** — MSP4021 touchscreen rendering + tap workflow verified.
2. ✅ **Calibration implemented** — single-phase guided wizard (50 Ω reference), all bands × 20 points, R/X offset table stored to EEPROM and applied (with linear interpolation) to every sweep.
3. Field-test the calibration routine on a real antenna (see `tests/`).

## License

This project is released under the **MIT License** — see [`LICENSE`](LICENSE).

### Third-party licenses

The firmware and tooling depend on the following open-source components, pulled at
build time by PlatformIO. They are **not** vendored into this repository, but are
required to build/run the project:

| Component | License | Used for |
|-----------|---------|----------|
| TFT_eSPI | MIT (BSD) | ST7796S display + XPT2046 touch driver |
| Arduino core (renesas-ra framework) | MIT | Uno R4 Minima Arduino API |
| Arduino SD library | MIT | Resolved by TFT_eSPI / build (unused) |
| Renesas RA board platform, toolchains | Apache-2.0 / various | PlatformIO build tooling |

These retain their own copyright/license terms. MIT is compatible with each of them;
no changes to this project's license are required.
