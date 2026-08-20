# Uno R4 Minima / RigExpert AA-30 Zero Interface

This project documents the hardware and software required to bridge the communication between an Arduino Uno R4 Minima and a RigExpert AA-30 Zero analyzer.

## 🛠️ Hardware Components
*   **UNO R4 Minima:** The main microcontroller board.
*   **AA-30 Zero:** The RF analyzer (Communicates via UART at 38400 baud).
*   **Wiring:** Connecting the specified pins mentioned in the current implementation.

## 📡 Communication Protocol
The AA-30 Zero uses UART at 38400 baud and supports multiple measured systems (25, 50, 75, 100 Ohm).

### Dual-Channel Bridging Setup
The system uses two separate software serial connections on the Uno R4 Minima to manage two independent communication channels:

| Channel | Uno Pin RX (Data In) | Uno Pin TX (Data Out) | Purpose |
| :--- | :--- | :--- | :--- |
| **Channel 1** | D1 | D0 | First data stream |
| **Channel 2** | D7 | D4 | Second data stream |

## 💾 Software Implementation (Arduino Sketch)
The bridge logic is contained in `AA30_Bridge/AA30_Bridge.ino`. This sketch utilizes the `#include <SoftwareSerial.h>` library to manage two independent 38400 baud serial connections.

*   **Goal:** Receive data from both AA-30 Zero channels and relay it back to the computer serial monitor for monitoring. It also relays commands sent from the monitor to both devices.
*   **Key Files:** `AA30_Bridge.ino`

## 🚀 Next Steps
1.  **Testing:** Connect the hardware and upload/test the sketch by using the Serial Monitor (9600 baud) to send test commands.
2.  **Command Definition:** Implement specific logic to interpret the incoming data (e.g., SWR, R, X values) and respond with necessary commands to control the AA-30 Zero.