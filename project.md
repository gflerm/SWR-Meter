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

| Command | Description | Purpose |
| :--- | :--- | :--- |
| `VER` | Returns analyzer type and firmware version. | Used for verification after power-up. |
| `XXXfqXXXXXXXXX` | Sets the center frequency (MHz). | Must be run to center the measurement. |
| `swXXXXXXXXX` | Sets the sweep frequency range (Hz). | Defines the measurable bandwidth. |
| `frxNNNN` | Performs N measurements. | Executes the physical scan. |
| `output frequency (MHz), R and X for every measurement` | Configures output format | Ensures continuous CSV stream: `Frequency,R,X\r\n`. |

### Operational Command Flow
The state machine in `AA30_Bridge.ino` will enforce this critical sequence:
1. **VER** $\rightarrow$ **CALIBRATE** $\rightarrow$ (Band Select $\rightarrow$ Frequency Set $\rightarrow$ Sweep Range Set) $\rightarrow$ **SCANNING** $\rightarrow$ **DISPLAYING** $\rightarrow$ **Idle**

## 💾 Software Implementation
*   **Bridge Functionality:** `AA30_Bridge.ino` manages the two serial lines, relays data, and handles button inputs.
*   **Measurement Logic:** The system state machine orchestrates the command sequence and handles data parsing and SWR calculation based on the protocol.

## ⚠️ Current Status: Blocked (Display Selection)
The project is currently blocked by the selection of the display hardware.

## 🚀 Next Steps
1.  **[CRITICAL] Select Display:** Finalize the display hardware choice. This determines the necessary libraries and UI code.
2.  **[HIGH PRIORITY] State Machine & Workflow:** Implement the state machine logic to govern the correct sequence: **IDLE $\rightarrow$ CALIBRATE $\rightarrow$ SELECT\_BAND $\rightarrow$ SCANNING $\rightarrow$ DISPLAYING**.
3.  **[HIGH PRIORITY] Band Control & Calibration:** Implement the necessary communication logic for band selection and the dedicated calibration sequence.
4.  **[HIGH PRIORITY] Data Processing:** Fully implement the robust parsing of the CSV stream and the SWR calculation formula.
5.  **[LOW PRIORITY] Verification:** Test the entire sequence using simulated data.