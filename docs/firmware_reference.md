# Firmware Reference — AA-30.ZERO SWR Meter

This document explains the firmware in `src/main.cpp` (PlatformIO + Arduino
framework, board `arduino:renesas_uno:minima` = Uno R4 Minima). It describes
the public types, the state machine, every function's purpose, and the serial
protocol used by the PC simulator.

> Source of truth: `src/main.cpp`. Doc comments in the source use Doxygen
> (`/** ... */`) so a tool like Doxygen could also regenerate this.

---

## 1. Hardware & pin map

| Signal | Uno R4 pin | Notes |
|--------|-----------|-------|
| AA-30 UART1 TX | D0 (`Serial1` RX) | Analyzer → R4 |
| AA-30 UART1 RX | D1 (`Serial1` TX) | R4 → Analyzer |
| Display CLK | D13 (SPI SCK) | ILI9341 |
| Display DIN | D11 (SPI COPI/MOSI) | ILI9341 |
| Display CS | D10 | ILI9341 |
| Display DC | D9 | ILI9341 |
| Display RST | D8 | ILI9341 |
| Display BL | 3.3 V | Not a GPIO |
| START button | D2 | INPUT_PULLUP, active-low |
| BAND button | D3 | INPUT_PULLUP, active-low |
| MODE button | D4 | INPUT_PULLUP, active-low |
| CAL button | D5 | INPUT_PULLUP, active-low |

AA-30 UART = **38400 baud**. PC/USB CDC `Serial` = **115200 baud**.

---

## 2. Key constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `Z0` | 50.0 | System impedance for SWR math |
| `MAX_RESISTANCE` | 1 000 000 Ω | Reject threshold for R |
| `MAX_REACTANCE` | 1 000 000 Ω | Reject threshold for \|X\| |
| `MAX_SWR` | 100.0 | Physically impossible above this |
| `LINE_BUF` | 96 | AA-30 line buffer size |
| `MAX_POINTS` | 256 | Points retained per scan |
| `POINTS_PER_SCAN` | 100 | Points requested per band |
| `DEBOUNCE_MS` | 25 | Button debounce |
| `SCAN_TIMEOUT_MS` | 8000 | Abort a hung sweep after this |
| `CAL_PTS_PER_BAND` | 20 | Calibration points per band |
| `CAL_EEPROM_MAGIC` | 0x4C4E31 | EEPROM table marker |
| `CAL_EEPROM_ADDR` | 0 | EEPROM base address |

---

## 3. Data types

### `struct Measurement`
A single validated measurement point.
```cpp
struct Measurement {
  float freqMHz; // frequency (MHz)
  float r;       // series resistance (ohm)
  float x;       // series reactance (ohm)
  float swr;     // computed SWR
  bool  valid;   // passed all checks
};
```

### `enum SystemState`
```cpp
enum SystemState {
  STATE_WELCOME,    // boot instructions page
  STATE_IDLE,       // main menu (select band/mode, scan, calibrate)
  STATE_CALIBRATE,  // calibration wizard (prompt or sweeping)
  STATE_CAL_DONE,   // calibration summary
  STATE_SCANNING,   // a band sweep is running
  STATE_DISPLAYING  // results shown
};
```

### `enum DisplayMode`
```cpp
enum DisplayMode { MODE_CURVE, MODE_NUMERIC };
```

### `struct Band` + `BANDS[]`
IARU Region 1 band plan (name + low/high edge in Hz). `NUM_BANDS` derives from
`sizeof(BANDS)/sizeof(BANDS[0])`.

### `enum CalPhase` + `CAL_REFS[]` / prompts
```cpp
enum CalPhase { CAL_PHASE_50, CAL_PHASE_SHORT, CAL_PHASE_OPEN };
```
`calPhasePrompt()` returns the user instruction for each phase.

### `struct CalPoint` / `struct CalBand`
```cpp
struct CalPoint { float freqMHz, rCorr, xCorr; };   // additive correction
struct CalBand  { bool valid; uint8_t count; CalPoint pts[CAL_PTS_PER_BAND]; };
```

---

## 4. State machine & code flow

The firmware is a finite state machine, stepped once per `loop()`:

```
setup()
 └─ pins, Serial1(38400), Serial(115200), EEPROM.begin(), loadCalibration(),
    tft begin/rotation, displayWelcome(), emitState("WELCOME")

loop()
 ├─ read 4 buttons (debounced, active-low)
 ├─ handlePcCommands()       ← PC soft-buttons + AA-30 passthrough
 ├─ (abort stuck scan / cal sweep → back to IDLE)
 ├─ handleStateMachine()
 │   ├─ WELCOME  → any button → IDLE
 │   ├─ IDLE     → BAND=cycle band, MODE=toggle layout,
 │   │             START=startScan, CAL=startCalibrate
 │   ├─ CALIBRATE→ (measuring → show progress; else)
 │   │             START=calBeginBandSweep, MODE=cancel
 │   ├─ CAL_DONE → any button → IDLE
 │   ├─ SCANNING → await collected points
 │   └─ DISPLAYING → MODE=toggle layout, START=IDLE
 ├─ pollAnalyzer()            ← assemble AA-30 lines → processLine()
 └─ delay(2)
```

### Scans
`START` in IDLE → `startScan()`: powers the RF board (`ON`), issues
`fq`/`sw`/`frx(N-1)`, enters `SCANNING`. Points stream back into
`pollAnalyzer()` → `processLine()` → `storePoint()` until
`POINTS_PER_SCAN` is reached, then `DISPLAYING`.

### Calibration wizard
`CAL` in IDLE → `startCalibrate()`: 3 phases, each sweeping every band × 20
points.

1. **50 Ω** — builds the per-band R/X offset table, saves to EEPROM.
2. **SHORT** — verifies (corrected \|R\|,\|X\| < 10 Ω).
3. **OPEN** — verifies (corrected \|R\| or \|X\| > 500 Ω).

Flow per band: `calBeginBandSweep()` → `calHandlePoint()` per point →
`calFinishBand()` → next band → `calFinishPhase()` → next phase →
`drawCalDone()` summary. `applyCalibration()` applies the nearest stored offset
to every normal (non-calibration) measurement.

---

## 5. Function reference

### Setup / loop
| Function | Purpose |
|----------|---------|
| `setup()` | Pin/UART/EEPROM/display init, load calibration, show welcome page. |
| `loop()` | Read buttons, step the state machine, drain the analyzer UART. |

### PC telemetry & commands
| Function | Purpose |
|----------|---------|
| `emitState(const char*)` | Emit `@STATE:<name>` for the simulator. |
| `emitBand()` | Emit `@BAND:<name>`. |
| `emitMode()` | Emit `@MODE:curve\|numeric`. |
| `emitCalPhase()` | Emit `@CALPHASE:<n>/<total>`. |
| `emitCalProgress()` | Emit `@CALPROG:band=<b>/<N>,pt=<p>/<P>`. |
| `handlePcCommands(bool& s, bool& b, bool& m, bool& c)` | Process PC serial: `!BTN:*` set button flags; `!GET:STATE` emits state; other lines pass through to the AA-30 in IDLE. |
| `isSweepStuck(uint32_t now)` | Detect a hung sweep (no data for `SCAN_TIMEOUT_MS`). |
| `showStatus(const char* msg, uint32_t ms)` | Show a transient on-screen message. |

### State machine & scans
| Function | Purpose |
|----------|---------|
| `handleStateMachine(bool, bool, bool, bool)` | Dispatch the FSM one step. |
| `startScan()` | Command the AA-30 to sweep the current band. |

### Calibration
| Function | Purpose |
|----------|---------|
| `calPhasePrompt()` | Text for the current phase's reference. |
| `startCalibrate()` | Enter the wizard (phase 1). |
| `calBeginBandSweep()` | Sweep the current band against the reference. |
| `calHandlePoint(const Measurement&)` | Store/record one calibration point. |
| `calFinishBand()` | Evaluate the band and advance to the next. |
| `calFinishPhase()` | Advance phase or finish the wizard. |
| `saveCalibration()` | Write the correction table to EEPROM. |
| `loadCalibration()` | Read + validate the table from EEPROM. |
| `applyCalibration(Measurement&)` | Apply nearest offset to a measurement. |

### Parsing & validation
| Function | Purpose |
|----------|---------|
| `isValidReading(float r, float x)` | Reject NaN/Inf or absurd R/X. |
| `computeSWR(float r, float x)` | SWR from series R/X in a `Z0` system. |
| `parseFRXLine(char*, Measurement&)` | Parse `freq,R,X` CSV line. |
| `storePoint(const Measurement&)` | Append to the scan buffer. |

### Analyzer polling
| Function | Purpose |
|----------|---------|
| `pollAnalyzer()` | Drain `Serial1`, assemble newline-terminated lines. |
| `processLine(char*)` | Dispatch one line (point / OK / message / bogus). |

### Display
| Function | Purpose |
|----------|---------|
| `displayWelcome()` | Draw the boot welcome/instructions screen. |
| `drawWelcome()` | Re-draw welcome from the state machine. |
| `updateDisplay()` | Render header + current page content. |
| `drawStatusOverlay()` | Overlay a transient message (e.g. "Aborted"). |
| `drawCalPrompt()` | Calibration prompt. |
| `drawCalProgress()` | Calibration band/point + progress bar. |
| `drawCalDone()` | Calibration PASS/FAIL summary. |
| `drawCurve(const Band&)` | SWR-vs-frequency curve. |
| `drawNumeric()` | Big F/R/X/SWR readout. |

---

## 6. Serial protocol (PC ↔ firmware)

### Firmware → PC (telemetry, lines start with `@`)
| Line | Meaning |
|------|---------|
| `@STATE:<name>` | Current state machine state. |
| `@BAND:<name>` | Selected band. |
| `@MODE:curve\|numeric` | Display layout. |
| `@POINT:<freq>,<R>,<X>,<SWR>` | A measured point (valid). |
| `@CALPHASE:<n>/<total>` | Calibration phase number. |
| `@CALPROG:band=<b>/<N>,pt=<p>/<P>` | Calibration progress. |

Also echoes human-readable lines: `Scanning <band> ...`, `<AA-30 OK>`,
`F=...MHz R=... X=... SWR=...`, `<AA-30> <msg>`, `<AA-30 BOGUS, discarded> ...`.

### PC → firmware (commands, lines start with `!`)
| Line | Meaning |
|------|---------|
| `!BTN:START` | Press/emulate the START button. |
| `!BTN:BAND` | Press/emulate the BAND button. |
| `!BTN:MODE` | Press/emulate the MODE button. |
| `!BTN:CAL` | Press/emulate the CAL button. |
| `!GET:STATE` | Request `@STATE:`/`@BAND:`/`@MODE:`. |

Any other line is forwarded verbatim to the AA-30 while in IDLE
(e.g. `ver`, `fq`, `sw`, `frx`, `ON`, `OFF`).

> Note: the AA-30 requires the RF board to be powered (`ON`) before a sweep
> returns data; `startScan()` and `calBeginBandSweep()` send `ON` first. The
> `fq`/`sw`/`frx` commands need a short gap between them or the analyzer can
> drop the sweep request.

---

## 7. Notes / gotchas

- **`%f` formatting** — do **not** use `snprintf(..., "%f", ...)`: newlib-nano
  linked without the float-printf object renders `%f` as blank. The source uses
  `dtostrf()` for telemetry instead.
- **Reset** — the firmware has no software reset; the welcome screen returns via
  the physical RESET button / power-cycle (software `NVIC`/1200-baud resets put
  the R4 into the DFU bootloader and drop the USB-CDC port).
- **Native USB** — the R4's USB re-enumerates after any reset; the simulator
  polls for the port to reappear.
