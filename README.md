# Uno R4 Minima / RigExpert AA-30 Zero SWR Meter

Software Standing Wave Ratio (SWR) Meter for testing antennas in the High Frequency (HF) bands (160m to 10m), built on an Arduino Uno R4 Minima with a RigExpert AA-30 Zero RF Analyzer.

## 💡 Project Scope
The system combines the Uno R4 Minima, the AA-30 Zero RF Analyzer, and a display unit to measure and display real-time SWR, Resistance (R), and Reactance (X) values across the specified HF frequency range (160m to 10m).

## 🛠️ Hardware Components
*   **UNO R4 Minima:** The main microcontroller board.
*   **AA-30 Zero:** The RF analyzer (Communicates via UART at 38400 baud).
*   **Display Unit:** To be selected (TBD).
*   **Control Buttons:** Four physical momentary push buttons:
    1. **Start Scan**
    2. **Mode Select**
    3. **Band Select**
    4. **Calibration**
*   **Wiring:** Connectors bridging the components.

## 📡 Communication Protocol
The AA-30 Zero uses UART at 38400 baud and supports multiple measured systems (25, 50, 75, 100 Ohm).

### Dual-Channel Bridging Setup
The system uses two separate software serial connections on the Uno R4 Minima:
*   **Channel 1:** D1 (RX) <--> D0 (TX)
*   **Channel 2:** D7 (RX) <--> D4 (TX)

### AA-30 Zero Command Protocol
The AA-30 Zero communicates via specific command strings over UART, sent in a precise sequential order:

| Command | Description | Purpose |
| :--- | :--- | :--- |
| `VER` | Returns analyzer type and firmware version. | Verification after power-up. |
| `XXXfqXXXXXXXXX` | Sets the center frequency (MHz). | Centers the measurement. |
| `swXXXXXXXXX` | Sets the sweep frequency range (Hz). | Defines the measurable bandwidth. |
| `frxNNNN` | Performs N measurements. | Executes the physical scan. |
| `output` | Configures output format. | Ensures continuous CSV stream: `Frequency,R,X\r\n`. |

### Operational Command Flow
`IDLE` → `CALIBRATE` → (Band Select → Frequency Set → Sweep Range Set) → `SCANNING` → `DISPLAYING` → `IDLE`

## 💾 Software Implementation
*   **Core Logic:** `AA30_Bridge.ino` implements a state machine approach managing the command sequence, data parsing, and SWR calculation.
*   **Reference:** The `images/protocol.jpg` shows the wiring diagram; a Processing reference sketch demonstrates SWR calculation from raw R and X values.

## 🚀 Next Steps
1.  **[CRITICAL] Select Display:** Finalize the display hardware choice.
2.  **[HIGH PRIORITY] State Machine & Workflow:** Implement the state machine logic for the full measurement workflow.
3.  **[HIGH PRIORITY] Band Control & Calibration:** Implement communication logic for band selection and calibration.
4.  **[HIGH PRIORITY] Data Processing:** Implement robust CSV stream parsing and SWR calculation.
5.  **[LOW PRIORITY] Verification:** Test the entire sequence using simulated data.