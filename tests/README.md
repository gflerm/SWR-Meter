# Firmware test suite

Deterministic end-to-end tests for the AA-30.ZERO SWR-meter firmware via the
Uno R4's USB-CDC port (COM8). See `docs/SPEC_SKELETON.md` for the build-and-
verify workflow that this suite supports.

## Run

```sh
python python/test_suite.py --port COM8            # all automated goals
python python/test_suite.py --port COM8 --goal G4  # one goal
python python/test_suite.py --manual               # print manual checklist
```

Exit codes: `0` = all run goals passed, `1` = at least one failed, `2` = no
board on the port.

## Goals

| Goal | Feature | Auto? | Manual? |
|------|---------|-------|---------|
| G1 | Boot + welcome screen | ✅ | 👁 LCD |
| G2 | Band selection cycles 160m..10m | ✅ | |
| G3 | Mode toggle curve/numeric | ✅ | |
| G4 | Scan produces data (fw scan agrees with passthrough) | ✅ | |
| G5 | Calibration wizard (50 ohm phase, single-phase) | ⚠️ | connect a 50 Ω load |
| G6 | Reset returns to welcome | | ✅ physical button |
| G7 | External control mode (`!CTRL:EXTERNAL/LOCAL`) | ✅ | |

## How it stays deterministic

The suite never races a blind timeout. It keys off the firmware's own signals:

- a scan is complete once `POINTS_PER_SCAN` (100) `@POINT:` lines are followed
  by a trailing `<AA-30 OK>`,
- state changes are signalled by `@STATE:<name>`,
- band/mode by `@BAND:`/`@MODE:`.

A generous `SETTLE` pause is inserted between commands so the board and the
AA-30 are not hammered (rapid commands cause dropped sweeps / missed state
changes).

## Log

Each run appends to `tests/test_log.md`.

## Manual steps (not automatable)

- G6 — press the physical RESET button; confirm the welcome screen returns and
  the USB port re-enumerates.
- G5 — attach a 50 Ω load at the wizard prompt and confirm it sweeps all bands
  and reports PASS (the table is saved to EEPROM).
- G1 — confirm the physical ILI9341 shows the welcome page.
- G7 — (**optional eye-check**) while `!CTRL:EXTERNAL` is active, the physical
  LCD shows the one-shot "EXTERNAL CONTROL" splash and then does not update.
