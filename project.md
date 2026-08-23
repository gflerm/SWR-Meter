# Uno R4 Minima / RigExpert AA-30 Zero Interface

This project documents the hardware and software required to build a Software Standing Wave Ratio (SWR) Meter for testing antennas in the High Frequency (HF) bands (160m to 10m).

## 💡 Project Scope: SWR Meter for HF Bands
The system combines the Uno R4 Minima, the AA-30 Zero RF Analyzer, and a yet-to-be-determined display unit to measure and display real-time SWR, Resistance (R), and Reactance (X) values across the specified HF frequency range.

## 🛠️ Hardware Components
*   **UNO R4 Minima:** The main microcontroller board.
*   **AA-30 Zero:** The RF analyzer (Communicates via UART at 38400 baud).
*   **Waveshare 2.4" SPI LCD (ILI9341):** The 240x320, 65K-colour display.
*   **Logic Level Converter (LEVEL-8P / TXS0108E, 8-ch):** Shifts the R4's 5 V SPI signals down to the display's 3.3 V logic.
*   **HU1–HU8 adapter board:** In-line breakout between the converter and the display.
*   **Control Buttons:** Four physical momentary push buttons (INPUT_PULLUP, debounced in firmware):
    1. **Start Scan** → D2
    2. **Band Select** → D3
    3. **Mode Select** → D4
    4. **Calibration** → D5
*   **Wiring:** Connectors bridging the components.

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

### Display Wiring (Waveshare 2.4" SPI / ILI9341 → Uno R4)
The display is driven on the R4's **fixed hardware SPI** (D11/COPI, D12/CIPO, D13/SCK) with
D10 as chip-select. The AA-30 occupies D0/D1 (Serial1) and the buttons D2–D5, so the
display pins D8–D13 are free — **no overlap**. Because the ILI9341 controller is 3.3 V and
the R4 outputs 5 V, all five logic signals pass through an **8-channel bi-directional level
converter** (LEVEL-8P / TXS0108E; `LV` = 3.3 V, `HV` = 5 V). Power, ground and backlight are
wired directly without conversion.

```
Display pin   Display     Level-8P            Uno R4
-----------   ----------  -----------------  -------------------
VCC           VCC (3.3V)  -                   Uno 3.3V (direct)
GND           GND         common GND          Uno GND (direct)
DIN           DIN (MOSI)  LV A1  <-  HV B1    Uno D11 (SPI COPI)
CLK           CLK (SCK)   LV A2  <-  HV B2    Uno D13 (SPI SCK)
CS            CS          LV A3  <-  HV B3    Uno D10 (chip select)
DC            DC          LV A4  <-  HV B4    Uno D9
RST           RST         LV A5  <-  HV B5    Uno D8
BL            BL          -                   Uno 3.3V (or D9 PWM)

Level-8P power:  LV side -> Uno 3.3V  |  HV side -> Uno 5V  |  GND common
```

> ⚠️ Keep the LV-side (3.3 V) wires short (<5 cm) to avoid overshoot on the TXS0108E.
>
> The **HU1–HU8 adapter board** is wired in-line between the converter's LV side and the
> display, passing the SPI signals plus VCC/GND straight through (its `TX/RX/GND` silkscreen
> labels are not used for this hook-up).

### Button Wiring (Buttons → Uno R4)
Four momentary push buttons, **INPUT_PULLUP** (pressed = LOW), debounced ~25 ms in firmware.
They read at 5 V directly — no level shifting.

```
START  ->  D2    (start a scan of the current band)
BAND   ->  D3    (cycle through the HF bands)
MODE   ->  D4    (toggle display layout: curve / numeric)
CAL    ->  D5    (run a one-off calibration scan)
```

### Operational Command Flow
The state machine in `src/main.cpp` enforces this critical sequence:
1. **IDLE** $\rightarrow$ (Band Select $\rightarrow$ Frequency Set $\rightarrow$ Sweep Range Set) $\rightarrow$ **SCANNING** $\rightarrow$ **DISPLAYING** $\rightarrow$ **Idle**
2. **CAL** routes through the same `fq`/`sw`/`frx` path to re-zero the reference.

## 💾 Software Implementation
Built with **PlatformIO** + Arduino framework, board `uno_r4_minima` (config: [`platformio.ini`](platformio.ini)).
*   **Bridge Functionality:** `src/main.cpp` manages the two serial lines (PC `Serial` + analyzer `Serial1`), relays data, and handles the four button inputs (START D2, BAND D3, MODE D4, CAL D5).
*   **Measurement Logic:** The system state machine orchestrates the command sequence and handles data parsing and SWR calculation based on the protocol.
*   **Bogus-Reading Guard:** Incoming `freq,R,X` lines are validated before use — NaN/Inf, physically impossible magnitudes (`R`/`|X|` > 1 MΩ), and out-of-range SWR (`<1` or `>100`) are discarded. Valid points are stored in `scanPoints[]` with computed SWR.
*   **Display/UI:** `scanPoints[]` are rendered to the ILI9341 as either an SWR-vs-frequency curve (green/yellow/red by SWR threshold) or a large R/X/SWR numeric readout. Requires Adafruit_GFX + Adafruit_ILI9341 (declared via `lib_deps`).

## ⚠️ Current Status
✅ **Uno R4 ⇄ AA-30.ZERO communication verified working** (using hardware `Serial1` @ 38400, analyzer on UART1). A 50 Ω dummy load reads correctly (R ≈ 50 Ω, X ≈ 0, SWR ≈ 1.0 across HF bands). Bogus-reading guard in place.
✅ **Full 160 m → 10 m sweep recorded** (IARU **Region 1**, 100 points/band) in [`result.md`](result.md) and [`result_data.json`](result_data.json); per-band and combined SWR graphs rendered to PDF in [`graphs/`](graphs).
🖥️ **Display selected**: Waveshare 2.4" SPI LCD (ILI9341, 240x320, 65K colour), 3.3 V logic.
🎛️ **Controls + UI implemented**: four buttons (START D2 / BAND D3 / MODE D4 / CAL D5) drive an ILI9341 SWR curve + numeric readout in `src/main.cpp`.
✅ **Calibration implemented**: guided wizard (50 Ω / short / open references) that sweeps all bands × 20 points, builds a per-band R/X offset table, stores it to EEPROM, and applies the correction to every normal sweep. Progress (band/point/bar) shown during calibration.
✅ **Builds cleanly with PlatformIO** (`renesas-ra` platform, `uno_r4_minima` board) — RAM 29.7%, Flash 25.2%.

## 🚀 Next Steps
1.  **Verify on hardware:** Confirm ILI9341 rendering + four-button workflow against a real display.
2.  **State Machine & Workflow:** Finalize **IDLE $\rightarrow$ CALIBRATE $\rightarrow$ SELECT\_BAND $\rightarrow$ SCANNING $\rightarrow$ DISPLAYING**.
3.  **Menu/Touch:** Optional rotary or touch control to extend band/mode selection.
4.  **[DONE] Verification:** Full-band sweep verified against a 50 Ω reference load across all HF bands; results and graphs generated.
5.  **[DONE] Calibration:** Guided wizard (50 Ω / short / open), all bands × 20 points, R/X offset table persisted to EEPROM and applied to sweeps.
