# Uno R4 Minima / RigExpert AA-30 Zero Interface

This project documents the hardware and software required to build a Software Standing Wave Ratio (SWR) Meter for testing antennas in the High Frequency (HF) bands (160m to 10m).

## 💡 Project Scope: SWR Meter for HF Bands
The system combines the Uno R4 Minima, the AA-30 Zero RF Analyzer, and a yet-to-be-determined display unit to measure and display real-time SWR, Resistance (R), and Reactance (X) values across the specified HF frequency range.

## 🛠️ Hardware Components
*   **UNO R4 Minima:** The main microcontroller board.
*   **AA-30 Zero:** The RF analyzer (Communicates via UART at 38400 baud).
*   **Display Unit:** To be selected (TBD).
*   **Control Buttons:** A robust instrument requires four physical momentary push buttons:
    1. **Start Scan**
    2. **Mode Select**
    3. **Band Select**
    4. **Calibration**
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

### Operational Command Flow
The state machine in `AA30_Bridge.ino` will enforce this critical sequence:
1. **VER** $\rightarrow$ **CALIBRATE** $\rightarrow$ (Band Select $\rightarrow$ Frequency Set $\rightarrow$ Sweep Range Set) $\rightarrow$ **SCANNING** $\rightarrow$ **DISPLAYING** $\rightarrow$ **Idle**

## 💾 Software Implementation
*   **Bridge Functionality:** `AA30_Bridge.ino` manages the two serial lines (PC `Serial` + analyzer `Serial1`), relays data, and handles button inputs.
*   **Measurement Logic:** The system state machine orchestrates the command sequence and handles data parsing and SWR calculation based on the protocol.
*   **Bogus-Reading Guard:** Incoming `freq,R,X` lines are validated before use — NaN/Inf, physically impossible magnitudes (`R`/`|X|` > 1 MΩ), and out-of-range SWR (`<1` or `>100`) are discarded. Valid points are stored in `scanPoints[]` with computed SWR.

## ⚠️ Current Status
✅ **Uno R4 ⇄ AA-30.ZERO communication verified working** (using hardware `Serial1` @ 38400, analyzer on UART1). A 50 Ω dummy load reads correctly (R ≈ 50 Ω, X ≈ 0, SWR ≈ 1.0 across HF bands). Bogus-reading guard in place.
✅ **Full 160 m → 10 m sweep recorded** (IARU **Region 1**, 100 points/band) in [`result.md`](result.md) and [`result_data.json`](result_data.json); per-band and combined SWR graphs rendered to PDF in [`graphs/`](graphs).
⏸️ **Still blocked on display selection** — final hardware choice determines the UI libraries and rendering code.

## 🚀 Next Steps
1.  **[CRITICAL] Select Display:** Finalize the display hardware choice. This determines the necessary libraries and UI code.
2.  **[HIGH PRIORITY] State Machine & Workflow:** Implement the state machine logic to govern the correct sequence: **IDLE $\rightarrow$ CALIBRATE $\rightarrow$ SELECT\_BAND $\rightarrow$ SCANNING $\rightarrow$ DISPLAYING**.
3.  **[HIGH PRIORITY] Band Control & Calibration:** Implement the necessary communication logic for band selection and the dedicated calibration sequence.
4.  **[HIGH PRIORITY] Data Processing:** Wire the implemented parsing/validation/SWR into the state machine and display.
5.  **[DONE] Verification:** Full-band sweep verified against a 50 Ω reference load across all HF bands; results and graphs generated.