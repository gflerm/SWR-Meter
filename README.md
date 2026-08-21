# Uno R4 Minima / RigExpert AA-30.ZERO SWR Meter

A bridge between an **Arduino Uno R4 Minima** and a **RigExpert AA-30.ZERO** antenna/cable
analyzer to measure and display Standing Wave Ratio (SWR), Resistance (R) and Reactance (X)
across the HF amateur bands (160 m → 10 m). The AA-30.ZERO works as an Arduino shield
(UART @ 38400 baud) and is a 5 V device.

## ✅ Status

- **Communication verified working** (`ver` → `AA-30.ZERO 200`, `fq`/`sw` → `OK`, `frx` → `freq,R,X` stream).
- Accurate measurement confirmed with a **50 Ω reference load** across all bands (R ≈ 50 Ω, SWR ≈ 1.0).
- Data validation guard in place to reject bogus readings (NaN/Inf, `R`/`|X|` > 1 MΩ, SWR out of range).
- Full 160 m → 10 m sweep recorded and graphed (IARU **Region 1** bands, 100 points/band).
- ⏳ Display selection still pending (affects UI library choice only).

## 🛠️ Hardware

| Component | Role |
|-----------|------|
| **Uno R4 Minima** (Renesas RA4M1) | Main controller |
| **AA-30.ZERO** | RF analyzer, 0.06–30 MHz, UART @ 38400 |
| Display (TBD) | To be selected |

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

- **`AA30_Bridge.ino`** — serial bridge + state machine. Parses and validates the `freq,R,X`
  stream, computes SWR, and stores valid points in `scanPoints[]`.
- **`python/`** — `sweep_bands.py` (drive the analyzer + record results) and
  `graph_results.py` (render PDF graphs with matplotlib).

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

## 📚 Documentation (`docs/`)

- [`AA-30_ZERO_schematics_and_drawings.pdf`](docs/AA-30_ZERO_schematics_and_drawings.pdf) — official schematics & BOM
- [`AA-30_ZERO_pcb_top.jpg`](docs/AA-30_ZERO_pcb_top.jpg) / [`AA-30_ZERO_pcb_bottom.jpg`](docs/AA-30_ZERO_pcb_bottom.jpg) — PCB drawings
- [`AA-30_ZERO_data_exchange_protocol.md`](docs/AA-30_ZERO_data_exchange_protocol.md) — command protocol
- [`AA-30_ZERO_getting_started.md`](docs/AA-30_ZERO_getting_started.md) — board overview & Arduino pairing

## 🔨 Build / Upload

```sh
arduino-cli compile --fqbn arduino:renesas_uno:minima AA30_Bridge
arduino-cli upload --fqbn arduino:renesas_uno:minima --port COM8 AA30_Bridge
```

USB CDC console: 9600 baud. AA-30 UART: 38400 baud (`Serial1`).

## 🚀 Next Steps

1. Select the display module and implement the UI rendering.
2. Complete the state machine (**IDLE → CALIBRATE → SELECT_BAND → SCANNING → DISPLAYING**).
3. Band control + calibration command logic.
4. Wire the parsed/validated data into the display/state machine.

## License

MIT — see [`LICENSE`](LICENSE).
