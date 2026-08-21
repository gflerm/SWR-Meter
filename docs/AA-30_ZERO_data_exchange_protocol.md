# RigExpert AA-30.ZERO — Serial Data Exchange Protocol

Source: RigExpert "Data exchange with RigExpert antenna analyzers" and "Getting started with .ZERO".
Verified against firmware `AA-30.ZERO 200` (this project, 38400 baud).

## Interface
- UART, **38400 baud**, 8 data bits, no parity, 1 stop bit.
- ASCII text commands. Commands and responses are terminated by CR/LF.
- Two on-board UARTs: **UART1** (pins 0/TX1, 2/RX1 → Arduino D0/D1) and **UART2** (pins 4/TX2, 7/RX2 → Arduino D4/D7). **Default = UART2.**

## Commands
| Command | Description | Response |
|---------|-------------|----------|
| `ver` | Returns analyzer type and firmware version. | `AA-30.ZERO XXX` |
| `fqXXXXXXXXX` | Set center frequency to XXXXXXXXX Hz. | `OK` |
| `swXXXXXXXXX` | Set sweep range to XXXXXXXXX Hz. | `OK` |
| `frxNNNN` | Perform NNNN measurement steps (NNNN+1 data points). | `freq,R,X\n` per point, then `OK` |
| `ON` | Turn on the RF board. | `OK` |
| `OFF` | Turn off the RF board. | `OK` |
| `flashfrxN` | Read stored data from memory N. | `freq,R,X` lines, then `OK` |

## Scan workflow
The recommended sequence for a measurement:

```
ON
FQ<center_frequency_hz>
SW<sweep_range_hz>
FRX<steps>
OFF
```

Example:
```
ON FQ145000000 SW10000000 FRX10 OFF
```

## Output format
`FRXn` outputs **n+1** points, one per line, CSV format:

```
<frequency_MHz>,<R_series_ohms>,<X_series_ohms>
```

then a final `OK`.

### Single-frequency measurement (firmware 2.0)
```
fq14100000   -> OK
sw0          -> OK     (zero sweep width)
frx0         -> <one line: 14.100000,R,X> then OK
```

## Notes / limits observed on this unit
- Firmware reports `AA-30.ZERO 200` for `ver`.
- Sample points scale with `sw`: the frequency step is `sw / (n)`, points span
  `center − sw/2 … center + sw/2`.
- Practical **max ~700 points** (n≈700 → 701 lines). `frx800` stalls on this unit.
- `R`/`X` are series resistance/reactance in ohms. With a 50 Ω system, SWR is
  computed from R and X (see project `AA30_Bridge.ino`).
- It is a **5 V** device; no level shifter required with a 5 V Arduino.

## Wiring on the Uno R4 Minima
The R4's `SoftwareSerial` cannot initialize on D4/D7, so use hardware `Serial1`
(D0 = RX, D1 = TX) with the analyzer's **UART1** interface:

```
AA-30.ZERO UART1 TX  ->  Uno D0  (Serial1 RX)
AA-30.ZERO UART1 RX  ->  Uno D1  (Serial1 TX)
AA-30.ZERO GND       ->  Uno GND
```
