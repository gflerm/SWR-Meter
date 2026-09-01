# Firmware Reference — AA-30.ZERO SWR Meter

This document explains the firmware (PlatformIO + Arduino framework, board
`arduino:renesas_uno:minima` = Uno R4 Minima). It describes the module layout,
public types, the state machine, every function's purpose, and the serial
protocol used by the PC simulator.

> Source of truth: `src/`. Doc comments in the source use Doxygen
> (`/** ... */`) so a tool like Doxygen could also regenerate this.

---

## 0. Module layout

The firmware is split into focused translation units, orchestrated by a thin
`main.cpp`. Shared cross-cutting state lives in `hardware.cpp` so the modules
do not depend on each other circularly.

| File | Responsibility |
|------|----------------|
| `main.cpp` | Orchestrator: `setup()`, `loop()`, the UI state machine, PC command handling. |
| `config.h` | Pin map, constants, and the shared data types / tables (no state, no objects). |
| `hardware.h`/`hardware.cpp` | Definitions of all global state, the `tft` display object, the GT911 `touch` object and the AA-30 `Serial1` port. |
| `display.h`/`display.cpp` | Every DFRobot DFR0669 (ILI9488) render call (`updateDisplay`, `draw*`) + the external-control splash. |
| `touch.h`/`touch.cpp` | GT911 capacitive touch scan + tap→action classifier. |
| `battery.h`/`battery.cpp` | LiPower MAX17043 fuel gauge: voltage, state-of-charge, low-batt alert (I2C 0x36). |
| `rigexpert.h`/`rigexpert.cpp` | AA-30 driver: UART polling, ASCII parser, validation, SWR math, `startScan`. |
| `calibration.h`/`calibration.cpp` | Calibration wizard, EEPROM table save/load, `applyCalibration`. |
| `telemetry.h`/`telemetry.cpp` | `emit*` telemetry writers, `showStatus`, `isSweepStuck`. |

Each module exposes its API via a header and hides its implementation. `config.h`
is included by every unit; `hardware.h` is included wherever a global or a
hardware object is touched.

---

## 1. Hardware & pin map

| Signal | Uno R4 pin | Notes |
|--------|-----------|-------|
| AA-30 UART1 TX | D0 (`Serial1` RX) | Analyzer → R4 |
| AA-30 UART1 RX | D1 (`Serial1` TX) | R4 → Analyzer |
| Display SCLK | D13 (SPI SCK) | DFR0669 (ILI9488) |
| Display MOSI | D11 (SPI COPI/MOSI) | DFR0669 (ILI9488) |
| Display CS | D10 | DFR0669 |
| Display DC | D9 | DFR0669 |
| Display RST | D8 | DFR0669 |
| Display VCC | 3.3–5.5 V | Direct (module is 3.3–5.5 V; **no level shifter**) |
| Display GND | GND | Direct |
| Display BL | on by default | Not a GPIO |
| Touch SDA | A4 | GT911 capacitive touch (I²C, addr 0x5D) |
| Touch SCL | A5 | GT911 capacitive touch (I²C) |
| LiPo gauge | A4/A5 | LiPower MAX17043 fuel gauge (I²C, addr 0x36, same bus) |
| LiPower 5 V out | 5 V | Powers R4 + DFR0669 (3.7 V LiPo boost) |
| Low-batt ALRT | D2 | MAX17043 alert (active-low), warns at ≤32 % |

AA-30 UART = **38400 baud**. PC/USB CDC `Serial` = **115200 baud**. UI is driven by
the GT911 **touchscreen** (no physical buttons); the **LiPower shield** powers the
unit and reports the battery.

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
| `TOUCH_DEBOUNCE_MS` | 40 | Touch hold time to register a press |
| `SCAN_TIMEOUT_MS` | 8000 | Abort a hung sweep after this |
| `TFT_W` / `TFT_H` | 480 / 320 | Landscape display size |
| `BAT_LOW_PCT` | 32 | Warn when battery drops to this % |
| `CAL_PTS_PER_BAND` | 20 | Calibration points per band |
| `CAL_MAX_RETRIES` | 3 | Single-point re-measures per bogus slot |
| `CAL_PASS_PCT` | 90 | Minimum % valid points to accept a band |
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

### `enum CalPhase`
```cpp
enum CalPhase { CAL_PHASE_50 };   // single-phase wizard (50 Ω builds the table)
```
`calPhasePrompt()` returns the user instruction for the current phase.

### `struct CalPoint` / `struct CalBand`
```cpp
struct CalPoint {
  float freqMHz;  // additive correction (freq anchor)
  float rCorr;    // add to measured R to get corrected R
  float xCorr;    // add to measured X to get corrected X
  bool  valid;    // this point produced a physically plausible reading
};
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
 ├─ poll GT911 touch → classify tap (START/BAND/MODE/CAL/any)
 ├─ handlePcCommands()       ← PC soft-buttons + AA-30 passthrough
 ├─ (abort stuck scan / cal sweep → back to IDLE)
 ├─ handleStateMachine()
 │   ├─ WELCOME  → any tap → IDLE
 │   ├─ IDLE     → BAND=cycle band, MODE=toggle layout,
 │   │             START=startScan, CAL=startCalibrate
 │   ├─ CALIBRATE→ (measuring → show progress; else)
 │   │             START=calBeginBandSweep, MODE=cancel
 │   ├─ CAL_DONE → any tap → IDLE
 │   ├─ SCANNING → await collected points
 │   └─ DISPLAYING → MODE=toggle layout, START=IDLE
 ├─ pollAnalyzer()            ← assemble AA-30 lines → processLine()
 └─ delay(2)
```

> UI is driven by the GT911 capacitive touch (I²C, 0x5D). The lower half of the
> screen is a row of four touch buttons (BAND | START | MODE | CAL); touching
> the upper half is "anywhere" (advance/dismiss). `!BTN:*` PC commands still
> work and are treated as the same actions.

### Scans
`START` in IDLE → `startScan()`: powers the RF board (`ON`), issues
`fq`/`sw`/`frx(N-1)`, enters `SCANNING`. Points stream back into
`pollAnalyzer()` → `processLine()` → `storePoint()` until
`POINTS_PER_SCAN` is reached, then `DISPLAYING`.

### Calibration wizard
`CAL` in IDLE → `startCalibrate()`: single phase sweeping every band × 20 points
against a **50 Ω** reference. Only 50 Ω matters (it builds the correction
table). SHORT/OPEN verification phases were removed — they were verification
only (no correction, no EEPROM write).

1. **50 Ω** — builds the per-band R/X offset table, saves to EEPROM.

Flow per band: `calBeginBandSweep()` → `calHandlePoint()` per point →
`calFinishBand()` → next band → `calFinishPhase()` (save + summary) →
`drawCalDone()`. `applyCalibration()` linearly interpolates the two nearest
stored offsets and applies them to every normal measurement.

**Bogus-point handling** — if a single point fails validation during a band
sweep, `processLine()` calls `calRetryPoint()` to re-measure that frequency
(via `fq<freq>[/sw0/frx0]`). After the band, `calFinishBand()` accepts the band
only if at least `CAL_PASS_PCT`% (90%) of its points are valid; otherwise the
band is marked invalid.

### External control
A host app can take over the unit via `!CTRL:EXTERNAL`. When set, the display
shows a one-shot "EXTERNAL CONTROL" splash and then `updateDisplay()` returns
immediately, so the loop runs at full speed for the host. `!CTRL:LOCAL` resumes
normal rendering.

---

## 5. Function reference

Where a function lives is shown in the "Module" column.

### Setup / loop (main.cpp)
| Function | Purpose |
|----------|---------|
| `setup()` | UART/EEPROM/display/touch init, load calibration, show welcome page. |
| `loop()` | Poll touch, step the state machine, drain the analyzer UART. |

### Touch (touch.cpp)
| Function | Purpose |
|----------|---------|
| `touchReadAction(uint16_t w, uint16_t h)` | Scan the GT911, debounce, and classify the press into a `TouchAction` (START/BAND/MODE/CAL/ANY). |

### Battery (battery.cpp)
| Function | Purpose |
|----------|---------|
| `batteryBegin()` | Init the MAX17043 (power-on reset + quick-start); 0 = OK. |
| `batteryVoltageMv()` | Read battery voltage in mV. |
| `batteryPercent()` | Read state-of-charge (0-100 %). |
| `batterySetAlert(uint8_t per)` | Set the low-batt alert threshold (1-32 %). |
| `batteryClearAlert()` | Clear a latched alert. |
| `batteryLowAlertActive()` | True if the ALRT pin (D2) is asserted (low battery). |

### PC telemetry & commands (telemetry.cpp / main.cpp)
| Function | Module | Purpose |
|----------|--------|---------|
| `emitState(const char*)` | telemetry | Emit `@STATE:<name>` for the simulator. |
| `emitBand()` | telemetry | Emit `@BAND:<name>`. |
| `emitMode()` | telemetry | Emit `@MODE:curve\|numeric`. |
| `emitCtrl()` | telemetry | Emit `@CTRL:external\|local`. |
| `emitCalPhase()` | telemetry | Emit `@CALPHASE:<n>/<total>`. |
| `emitCalProgress()` | telemetry | Emit `@CALPROG:band=<b>/<N>,pt=<p>/<P>`. |
| `handlePcCommands(bool& s, bool& b, bool& m, bool& c)` | main | Process PC serial: `!BTN:*` set the corresponding action flags (same as a touch tap); `!CTRL:*` toggle external control; `!GET:STATE` emits state; other lines pass through to the AA-30 in IDLE. |
| `isSweepStuck(uint32_t now)` | telemetry | Detect a hung sweep (no data for `SCAN_TIMEOUT_MS`). |
| `showStatus(const char* msg, uint32_t ms)` | telemetry | Show a transient on-screen message. |

### State machine & scans (main.cpp / rigexpert.cpp)
| Function | Module | Purpose |
|----------|--------|---------|
| `handleStateMachine(bool, bool, bool, bool)` | main | Dispatch the FSM one step. |
| `startScan()` | rigexpert | Command the AA-30 to sweep the current band. |

### Calibration (calibration.cpp)
| Function | Purpose |
|----------|---------|
| `calPhasePrompt()` | Text for the current phase's reference. |
| `startCalibrate()` | Enter the wizard (phase 1). |
| `calBeginBandSweep()` | Sweep the current band against the reference. |
| `calHandlePoint(const Measurement&)` | Store/record one calibration point. |
| `calRetryPoint(float)` | Re-measure a single bogus frequency. |
| `calFinishBand()` | Evaluate the band (90% gate) and advance to the next. |
| `calFinishPhase()` | Save the table + show the result summary. |
| `saveCalibration()` | Write the correction table to EEPROM. |
| `loadCalibration()` | Read + validate the table from EEPROM. |
| `applyCalibration(Measurement&)` | Linearly interpolate + apply nearest stored offsets. |

### Parsing & validation (rigexpert.cpp)
| Function | Purpose |
|----------|---------|
| `isValidReading(float r, float x)` | Reject NaN/Inf or absurd R/X. |
| `computeSWR(float r, float x)` | SWR from series R/X in a `Z0` system. |
| `parseFRXLine(char*, Measurement&)` | Parse `freq,R,X` CSV line. |
| `storePoint(const Measurement&)` | Append to the scan buffer. |

### Analyzer polling (rigexpert.cpp)
| Function | Purpose |
|----------|---------|
| `pollAnalyzer()` | Drain `Serial1`, assemble newline-terminated lines. |
| `processLine(char*)` | Dispatch one line (point / OK / message / bogus). |

### Display (display.cpp)
| Function | Purpose |
|----------|---------|
| `displayWelcome()` | Draw the boot welcome/instructions screen. |
| `drawWelcome()` | Re-draw welcome from the state machine. |
| `updateDisplay()` | Render header + current page content. |
| `drawExternalSplash()` | One-shot "EXTERNAL CONTROL" page. |
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
| `@CTRL:external\|local` | Who owns the unit / whether the display is bypassed. |
| `@POINT:<freq>,<R>,<X>,<SWR>` | A measured point (valid). |
| `@CALPHASE:<n>/<total>` | Calibration phase number. |
| `@CALPROG:band=<b>/<N>,pt=<p>/<P>` | Calibration progress. |
| `@CALBAND:band=<b>,pass=<0\|1>` | Per-band pass/fail during a cal phase. |
| `@CALRESULT:PASS\|FAIL` | Overall wizard verdict. |

Also echoes human-readable lines: `Scanning <band> ...`, `<AA-30 OK>`,
`F=...MHz R=... X=... SWR=...`, `<AA-30> <msg>`, `<AA-30 BOGUS, discarded> ...`,
`Retry cal point @ <freq>`.

> **Buffered / verified output** — `@POINT:` lines are produced on the fly for
> normal scans. During calibration the raw stream is checked and individual
> bogus points are re-measured; only well-formed points feed the wizard.
> When `@CTRL:external` is active the display is bypassed (no redraws) and the
> loop runs as fast as possible for the host.

### PC → firmware (commands, lines start with `!`)
| Line | Meaning |
|------|---------|
| `!BTN:START` | Emulate a START tap. |
| `!BTN:BAND` | Emulate a BAND tap. |
| `!BTN:MODE` | Emulate a MODE tap. |
| `!BTN:CAL` | Emulate a CAL tap. |
| `!CTRL:EXTERNAL` | Host takes control; bypass display rendering. |
| `!CTRL:LOCAL` | Return to local control; resume rendering. |
| `!GET:STATE` | Request `@STATE:`/`@BAND:`/`@MODE:`/`@CTRL:`. |

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
- **No RTOS (by design)** — proven issues here are data integrity, not
  scheduling, so the firmware intentionally stays a single cooperative event
  loop. A scheduler would add RAM per task, require mutexes around the shared
  globals, and introduce concurrency bugs for no measurable gain.
- **Buffered / verified scans** — during calibration each band's points are
  checked and individual bogus measurements are re-tried; a band is accepted
  only if ≥ `CAL_PASS_PCT`% of its points are valid.
- **Reset** — the firmware has no software reset; the welcome screen returns via
  the physical RESET button / power-cycle (software `NVIC`/1200-baud resets put
  the R4 into the DFU bootloader and drop the USB-CDC port).
- **Native USB** — the R4's USB re-enumerates after any reset; the simulator
  polls for the port to reappear.
