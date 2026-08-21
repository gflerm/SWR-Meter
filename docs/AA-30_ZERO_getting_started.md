# RigExpert AA-30.ZERO — Getting Started (summary)

Source: RigExpert "Getting started with .ZERO". The full article is available at
https://rigexpert.com/news/getting-started-with-zero/ and
https://old.rigexpert.com/products/antenna-analyzers/aa-30-zero/getting-started-with-the-zero/

## What it is
- AA-30.ZERO is a miniature antenna/cable analyzer in **Arduino UNO shield format**
  (PCB only, no case/display/keyboard).
- Frequency range **0.06 – 30 MHz**, 1 Hz frequency entry.
- Measures a **25 / 50 / 75 / 100 Ω** system (default 50 Ω).
- Communication interface: **UART, 38400 baud**.
- Supply: **external 5 V**, ~150 mA max.
- RF output on SMA; output power +13 dBm (50 Ω load).

## Connecting to a PC
It has no USB circuitry — use an off-board USB-to-UART-TTL converter:
- USB-UART **TX → analyzer RX**
- USB-UART **RX → analyzer TX**
- common GND

## Pairing with Arduino
Solder the supplied breakaway headers, then plug the .ZERO onto the Arduino.

| Arduino pin | Function |
|-------------|----------|
| D0 | UART interface 1, TX, data out |
| D1 | UART interface 1, RX, data in |
| D4 | UART interface 2, TX, data out |
| D7 | UART interface 2, RX, data in |

Default active UART is **UART2** (D4/D7). Select UART1 vs UART2 via the
on-board selection jumpers.

## Reading UART2 is the default
The official Arduino bridge example uses `SoftwareSerial ZERO(4, 7); // RX, TX`
(i.e. RX on pin 4, TX on pin 7) for the default UART2.

## Firmware
- Firmware **2.0** added single-frequency measurement (`frx0`).
- The `RigExpertZero` Arduino library requires firmware ≥ 2.0; update with the
  FlashTool utility.
- This unit reports `AA-30.ZERO 200`.

## Sources / downloads
- Getting started: https://rigexpert.com/news/getting-started-with-zero/
- Product page: https://rigexpert.com/products/kits/aa-30-zero/
- Schematics & BOM: `docs/AA-30_ZERO_schematics_and_drawings.pdf`
- PCB drawings (TOP/BOTTOM): `docs/AA-30_ZERO_pcb_top.jpg`, `docs/AA-30_ZERO_pcb_bottom.jpg`
