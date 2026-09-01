# Engineering Spec Skeleton — a controlled build-and-verify workflow

A reusable template for planning, building, and *proving* a feature before
moving on. It exists so you don't repeat the mistake of running ahead of
verification: define a goal, implement it, prove it in isolation, log it, and
commit it — **then** start the next goal. Capabilities that need human/manual
actions (physical buttons, swapping hardware loads, a screen you must eyeball)
are called out explicitly rather than faked by automation.

> This skeleton is deliberately general. The bracketed `[EXAMPLE...]` sections
> show how it was applied to the AA-30.ZERO / Arduino Uno R4 SWR-meter project.
> The "Lessons learned" section records real pitfalls hit along the way.

---

## 1. Purpose & problem statement

> **What is this project/system, and what problem does it solve?**
>
> [EXAMPLE] A bench SWR meter: an Uno R4 Minima reads a RigExpert AA-30.ZERO
> antenna analyzer (UART @ 38400) and shows SWR/|Z| on a 3.5" DFRobot DFR0669
> ILI9488 capacitive-touch display. A Windows-side simulator lets the PC drive
> the unit over the USB-CDC port for verification.

## 2. Scope & non-goals

> **What is IN scope for this release/iteration? What is explicitly OUT?**
>
> - In: [example] boot, band select, mode toggle, band sweep, calibration
>   wizard (single-phase, 50 ohm reference), PC simulator.
> - Out: [example] calibration *correction correctness* under a real antenna
>   load, touchscreen, RF measurements above 170 MHz.

## 3. Requirements / acceptance criteria (goal by goal)

> Each feature becomes a numbered goal with a concrete, testable PASS
> condition. Do not start goal N+1 until goal N is proven and committed.

| Goal | Feature | PASS condition (observable) | Auto? | Manual? |
|------|---------|-----------------------------|-------|---------|
| G1 | [boot + welcome] | [emits @STATE:WELCOME; LCD shows welcome page] | ✅ | 👁 screen |
| G2 | [band selection] | [BAND cycles 160m..10m; @BAND: updates each press] | ✅ | |
| G3 | [mode toggle] | [MODE flips curve/numeric; @MODE: changes] | ✅ | |
| G4 | [scan produces data] | [full sweep; N points + terminal OK; values plausible + agree with reference path] | ✅ | |
| G5 | [calibration wizard] | [single-phase 50-ohm run; reports DONE/FAILED & saves table] | ⚠️ partial | attach 50 Ω load |
| G6 | [reset → welcome] | [physical reset restores welcome; port re-enumerates] | | ✅ |
| G7 | [external control] | [`!CTRL:EXTERNAL` reports `@CTRL:external` & bypasses the display; unit stays responsive; `!CTRL:LOCAL` resumes] | ✅ | 👁 LCD splash |
| G8 | [touchscreen + LCD] | [each on-screen touch zone (BAND/START/MODE/CAL) drives the matching action; the DFR0669 shows the correct page immediately] | | ✅ touch + 👁 LCD |

## 4. Architecture

> Brief description of the components and how they talk.

- Device firmware [src/main.cpp]
  - state machine [WELCOME/IDLE/CALIBRATE/SCANNING/DISPLAYING]
  - UART protocol [38400 to analyzer; 115200 USB-CDC telemetry]
- Host/simulator [python/sim_display.py]
- Test harness [python/test_suite.py]
- Docs [docs/firmware_reference.md]

### Serial protocols (define once, generate telemetry + commands)

| Direction | Frame | Meaning |
|-----------|-------|---------|
| fw → PC | `@STATE:<x>` | current state |
| fw → PC | `@POINT:f,r,x,swr` | a valid measurement point |
| PC → fw | `!BTN:<name>` | inject a button press |
| PC → fw | `!GET:STATE` | request a status snapshot |

> Design rule: **give the host deterministic completion signals.** If the
> device can signal "this batch is done" (e.g. a trailing `OK`, or an exact
> point count), the harness should key off that, never a blind timer.

## 5. Implementation plan

> The ordered, one-goal-at-a-time build order. Each step ends in a **local
> commit** so you can always roll back.

1. [G1] boot + welcome
2. [G2] band selection
3. [G3] mode toggle
4. [G4] scan + telemetry (the core data path)
5. [G5] calibration wizard
6. [G6] reset path

## 6. Test procedure

> A deterministic test suite, plus an explicit manual checklist for anything
> automation can't observe.

### Automated suite
- Entry point: [python/test_suite.py --port COM8]
- Pass/fail per goal; a run log is written to tests/test_log.md.
- **Settle time:** a configurable pause between commands. [SETTLE = 0.6 s]

### Manual checklist (cannot be automated)
- [G6] press the physical RESET; confirm welcome returns + port re-enumerates.
- [G5] attach a 50 Ω load, run the wizard, confirm PASS and that the table is saved to EEPROM.
- [G1/👁] confirm the physical LCD shows the welcome page (host can't see it).
- [G8/👁] tap each on-screen touch zone (BAND/START/MODE/CAL) and confirm the DFR0669
  updates to the matching page each time.

## 7. Results & verification record

> One row per goal, updated after each run. Attach the run log.

| Goal | Date | Result | Notes |
|------|------|--------|-------|
| G1 | [date] | ✅/❌ | | 
| G2 | | | |
| G3 | | | |
| G4 | | | |
| G5 | | | partial — needs a 50 Ω load |
| G6 | | | manual |
| G7 | 2026-08-23 | ✅ 2026-08-23 | |
| G8 | | | manual — buttons + LCD |

## 8. Rollback & versioning

- **Local commit per goal** so any regression is one `git revert`/checkout away.
- Before starting, tag/note the last "known-good" commit.
- After all goals pass + reports are produced, the upstream (remote) push may
  be issued.

---

## Lessons learned (from a real project)

1. **"It was working before" usually means the harness broke, not the device.**
   In this project the AA-30 was always returning valid data; the flaky
   *Python harness* raced timeouts and read 0/2/3 points, making the firmware
   look broken.

2. **Never race a blind timeout when the device offers deterministic signals.**
   We collect `POINTS_PER_SCAN` points + a trailing `OK`. That removed the
   "0 points this run, 99 next run" flakiness for good.

3. **Hammering a device produces false failures.** Rapid-fire commands made
   the analyzer drop sweeps and made the MCU miss state transitions. A real
   "settle" delay between commands mirrors normal, unhurried use. Build it
   into the suite, not into ad-hoc sleeps in a test script.

4. **Greedy regexes silently parse the wrong field.** `@BAND:(\S+)` grabbed
   `20m@MODE:curve` when lines were concatenated. Scope the pattern to the
   field only: `@BAND:([^@\r\n]+)`.

5. **Confound the code path under test, not the whole stack.** G4 compared the
   firmware scan against a direct passthrough sweep and demanded they agree —
   proving both the firmware state machine **and** the analyzer plumbing.

6. **State what needs a human before automating.** Reset (physical button) and
   attaching a calibration load can't be scripted. Marking them "manual" in
   the procedure is honest and avoids a fake green.

7. **Separate "compile-clean" from "works".** A sketch can build fine yet print
   blank telemetry (here: `%f` with newlib-nano lacking float-printf). Test the
   *output values*, not just that it compiles.

8. **Document as you go, from the code.** Doxygen-style comments on each
   function (src/main.cpp) let you generate docs/firmware_reference.md rather
   than hand-writing a stale description.

---

## Templates to copy

### Per-goal commit message
```
Add <feature> (G<n>)

<what it does> and how to verify.
Verified: <observable result / test output summary>.
Manual steps remaining: <anything not automatable>.
```

### Test-suite skeleton (pseudo-code)
```python
SETTLE = 0.6            # seconds between commands

def pause(): time.sleep(SETTLE)

def collect_points(start_fn, want, timeout):
    start_fn()
    pts = []
    while len(pts) < want and time.time() < deadline:
        pts += parse_lines(read())
        if ok_seen and pts: break
    return pts

def test_goal(fw):
    to_idle(fw); pause()
    pts, ok = collect_points(lambda: fw.write("!BTN:START"), want, timeout)
    assert plausible(pts) and ok
```
