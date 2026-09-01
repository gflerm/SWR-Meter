---
title: UnoR4_AA.Zero30_SWR-Meter
description: Uno R4 Minima + AA-30 Zero HF SWR meter measuring R/X across 160m-10m.
status: active
priority: medium
startDate: "2026-08-29"
tags: [firmware, arduino, ham-radio, swr, uno-r4]
---

# Uno R4 Minima / RigExpert AA-30 Zero Interface

This project documents the hardware and software required to build a Software Standing Wave Ratio (SWR) Meter for testing antennas in the High Frequency (HF) bands (160m to 10m).

## 💡 Project Scope: SWR Meter for HF Bands
The system combines the Uno R4 Minima, the AA-30 Zero RF Analyzer, and a yet-to-be-determined display unit to measure and display real-time SWR, Resistance (R), and Reactance (X) values across the specified HF frequency range.

## 🛠️ Hardware Components
*   **UNO R4 Minima:** The main microcontroller board.
*   **AA-30 Zero:** The RF analyzer (Communicates via UART at 38400 baud).
*   **LCDWIKI MSP4021 4.0" TFT (ST7796S):** The 480x320 display with XPT2046 resistive touch; driven by TFT_eSPI on shared SPI.
*   **LiPower Shield 0.5A (ACS33721L):** 3.7 V LiPo → 5 V power + MAX17043 fuel gauge (I2C 0x36), low-batt alert on D2.
*   **Control:** on-screen XPT2046 resistive touch (BAND / START / MODE / CAL zones).
*   **Wiring:** connectors bridging the components (SPI D6–D13 + battery I²C A4/A5).

## 📡 Communication Protocol
The AA-30 Zero uses UART at 38400 baud and supports multiple measured systems (25, 50, 75, 100 Ohm).

### AA-30 Zero Command Protocol
The AA-30 Zero communicates via specific command strings over UART. These commands must be sent in a precise, sequential order:

| Command | Description | Response |
| :--- | :--- | :--- |
| `ver` | Returns analyzer type and firmware version. | `AA-30 ZERO XXX` |
| `fqXXXXXXXXXX` | Sets the center frequency to XXXXXXXXXX Hz. | `OK` |
| `swXXXXXXXXXX` | Sets the sweep range to XXXXXXXXXX Hz. | `OK` |
| `frxNNNN` | Performs NNNN measurements in the specified range. | Outputs `Frequency (MHz), R, X` for every measurement. |

> **Verified working**: `ver` returns `AA-30.ZERO 200`, `fq`/`sw` return `OK`, and `frxNNNN` streams `freq,R,X` lines ending in `OK`.

### ⚠️ UNO R4 Minima UART Constraint (IMPORTANT)
The AA-30.ZERO has **two** UARTs (default **UART2** on D4/D7, UART1 on D0/D1). On the **Uno R4 Minima**:

*   **Use hardware `Serial1` (D0 = RX, D1 = TX) → analyzer UART1.** This is the only reliable path, verified working at 38400 baud.
*   **Do NOT use SoftwareSerial** for the analyzer: the R4's SoftwareSerial cannot initialize on D4/D7 (`SoftwareSerial.begin()` returns 0), so the default UART2 cannot be reached this way.
*   The analyzer must be **set to UART1** (its on-board UART selection; default is UART2).

**Wiring (analyzer UART1 → Uno R4):**
```
AA-30.ZERO UART1 TX  ->  Uno D0  (Serial1 RX)
AA-30.ZERO UART1 RX  ->  Uno D1  (Serial1 TX)
AA-30.ZERO GND       ->  Uno GND (common ground required)
```
The AA-30.ZERO is a 5 V device; a level shifter is not required.

### Display + Touch Wiring (LCDWIKI MSP4021 → Uno R4)
The display is driven on the R4's **fixed hardware SPI** (D11/COPI, D13/SCK,
D12/CIPO) with D8–D10 for RST/DC/CS and D7 for the backlight. The XPT2046 touch
shares the SPI bus (its own CS on D6, IRQ on A0). The AA-30 occupies D0/D1
(Serial1) and the battery I²C uses A4/A5; the vendor's UNO map uses A4/A5 for
the display, which are moved to D8/D10 on the R4.

```
MSP4021 pin    Uno R4
--------------  ----------------
VCC            5 V (direct)
GND            GND (direct)
SCK            D13 (SPI SCK)
SDI(MOSI)      D11 (SPI COPI)
SDO(MISO)      D12 (SPI CIPO, optional)
CS             D10
DC/RS          D9
RESET          D8
LED            D7 (backlight)
T_CS           D6 (XPT2046)
T_IRQ          A0 (XPT2046)
```

> ✅ The display/touch use D6–D13 (SPI) + A0, the AA-30 uses D0/D1, and the
> battery gauge I²C uses A4/A5 — **no overlap**. Touch shares MOSI/SCK/MISO with
> the display (SJ1-SJ3 jumpered).

### Touch UI
The XPT2046 resistive touch is the only control; the lower half of the screen is a
row of four touch buttons (**BAND | START | MODE | CAL**), upper half = "anywhere".

### Operational Command Flow
The state machine in `src/main.cpp` enforces this critical sequence:
1. **IDLE** $\rightarrow$ (Band Select $\rightarrow$ Frequency Set $\rightarrow$ Sweep Range Set) $\rightarrow$ **SCANNING** $\rightarrow$ **DISPLAYING** $\rightarrow$ **Idle**
2. **CAL** routes through the same `fq`/`sw`/`frx` path to re-zero the reference.

## 💾 Software Implementation
Built with **PlatformIO** + Arduino framework, board `uno_r4_minima` (config: [`platformio.ini`](platformio.ini)).
*   **Modular structure:** `src/main.cpp` is a thin orchestrator (`setup`/`loop`/state machine/PC commands). Handlers live in their own modules: `display.*` (MSP4021/ST7796S render), `touch.*` (XPT2046 scan + classifier), `battery.*` (MAX17043 gauge), `rigexpert.*` (AA-30 parser/scans), `calibration.*` (wizard/EEPROM), `telemetry.*` (`@`-telemetry), plus `config.h` (pins/types) and `hardware.h`/`.cpp` (shared global state + `tft` + `Serial1`).
*   **Bridge Functionality:** manages the two serial lines (PC `Serial` + analyzer `Serial1`) and relays data.
*   **Measurement Logic:** the state machine orchestrates the command sequence, parsing and SWR calculation based on the protocol.
*   **Validation:** Incoming `freq,R,X` lines are parsed **syntactically** (finite, in-range) then gated for physical plausibility on normal sweeps (`R`/`|X|` sensible, SWR in `[1,100]`). NaN/Inf and absurd magnitudes are discarded. Valid points are stored in `scanPoints[]` with computed SWR.
*   **Display/UI:** `scanPoints[]` are rendered to the MSP4021 as either an SWR-vs-frequency curve (green/yellow/red by SWR threshold) or a large R/X/SWR numeric readout. Input is the XPT2046 resistive touchscreen (TFT_eSPI).
*   **External control:** a host app can take over with `!CTRL:EXTERNAL` (display bypassed for speed) and resume with `!CTRL:LOCAL`.

## ⚠️ Current Status
✅ **Uno R4 ⇄ AA-30.ZERO communication verified working** (using hardware `Serial1` @ 38400, analyzer on UART1). A 50 Ω dummy load reads correctly (R ≈ 50 Ω, X ≈ 0, SWR ≈ 1.0 across HF bands). Bogus-reading guard in place.
✅ **Full 160 m → 10 m sweep recorded** (IARU **Region 1**, 100 points/band) in [`result.md`](result.md) and [`result_data.json`](result_data.json); per-band and combined SWR graphs rendered to PDF in [`graphs/`](graphs).
🖥️ **Display**: LCDWIKI MSP4021 4.0" ST7796S TFT (480×320), XPT2046 resistive touch (TFT_eSPI).
🎛️ **Controls + UI**: XPT2046 resistive touchscreen (BAND/START/MODE/CAL zones) drives the SWR curve + numeric readout.
🔋 **Power + monitoring**: LiPower Shield 0.5A (3.7 V LiPo → 5 V) with MAX17043 fuel gauge; battery % on the display, low-batt alert (D2). Read the battery only with the shield switch ON (gauge reads ~1 V when off); I2C pull-ups must be fitted.
✅ **Calibration implemented**: single-phase guided wizard (50 Ω reference) that sweeps all bands × 20 points, builds a per-band R/X offset table, stores it to EEPROM, and applies the correction (with linear interpolation between table points) to every normal sweep. Progress (band/point/bar) shown during calibration.
✅ **Modularized + robust**: split into focused modules; SHORT/OPEN verification phases removed (verification-only, no correction/EEPROM write); outlier-tolerant 90% band-vote; live-sweep watchdog fix; scan completes on trailing `OK`.
✅ **Builds cleanly with PlatformIO** (`renesas-ra` platform, `uno_r4_minima` board) — RAM 44.3%, Flash 34.7%.
✅ **Automated test suite (G1–G7)**: boot/welcome, band selection, mode toggle, scan data (with passthrough agreement), single-phase calibration to `CAL_DONE`, and external-control mode. All 6 runnable goals pass; G6 (physical reset) and G8 (touchscreen + LCD render) are manual.

## 🚀 Next Steps
1.  **Verify on hardware:** Confirm MSP4021 rendering + XPT2046 touch workflow against the real module.
2.  **Menu/UI:** extend band/mode selection via the touchscreen.
3.  **[DONE] Verification:** Full-band sweep verified against a 50 Ω reference load across all HF bands; results and graphs generated.
4.  **[DONE] Calibration:** Single-phase guided wizard (50 Ω), all bands × 20 points, R/X offset table persisted to EEPROM and applied (with interpolation) to sweeps.
