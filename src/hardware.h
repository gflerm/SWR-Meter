// hardware.h
//
// A single home for the R4's shared global state: the system state machine
// variables, the scan point buffer, the analyzer line assembler, calibration
// state, and the two physical hardware objects (the ILI9341 display and the
// AA-30 UART). Every module includes this header and uses the `extern`
// declarations; the definitions live in hardware.cpp.
//
// Keeping all cross-cutting state here breaks the otherwise circular
// dependency between the display, rigexpert and calibration modules.
//
// 2024, opencode AI

#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "config.h"

// ---- hardware objects ------------------------------------------------

// ILI9341 SPI display (320x240).
extern Adafruit_ILI9341 tft;

// ---- machine-wide state ----------------------------------------------

extern SystemState currentState;
extern DisplayMode displayMode;
extern uint8_t     bandIndex;

// External-app control: when a host program takes over, show a "external
// control" splash once and then bypass ALL display rendering to keep the
// UART/analyzer path fast. Toggled by `!CTRL:EXTERNAL` / `!CTRL:LOCAL`.
extern bool extControl;
extern bool extSplashDone;

// The last completed scan, retained for display.
extern Measurement scanPoints[MAX_POINTS];
extern uint16_t    scanCount;

// AA-30 line assembler (fixed buffer, no String / heap use).
extern char      lineBuf[LINE_BUF];
extern uint8_t   lineLen;
extern bool      collecting;  // true while awaiting frx data

// Timestamp when the last scan/calibration sweep started (for timeout).
extern uint32_t  sweepStart;

// Transient on-screen message (e.g. "Aborted") shown briefly after an event.
extern const char* statusMsg;
extern uint32_t    statusMsgUntil;

// PC command line assembler (lines starting with '!' are device commands).
extern char      pcCmdBuf[LINE_BUF];
extern uint8_t   pcCmdLen;

// ---- calibration / performance-check state ---------------------------

extern uint8_t    calPhase;           // current wizard phase
extern uint8_t    calBandIndex;       // band currently being swept
extern uint8_t    calPoint;           // points collected for current band
extern bool       calActive;          // wizard active
extern bool       calMeasuring;       // a sweep is in progress
extern bool       calFinishPending;   // guard: band already being finished
extern bool       calVerifying;       // post-sweep verify/retry phase
extern float      calRetryFreq;       // freq of a bogus point being retried
extern bool       calRetrying;        // a single-point retry is in flight
extern uint8_t    calRetryIdx;        // band slot currently being retried
extern uint8_t    calRetryUsed[CAL_PTS_PER_BAND]; // retries per band slot
extern bool       calDone;            // wizard finished (summary shown)
extern bool       calPassed;          // overall pass/fail of the wizard
extern uint8_t    calFailCount;       // bands that failed (per phase)
extern uint8_t    calTotalPass;       // bands passed across all phases
extern CalResult  calResult;

// Correction tables (RAM copy, persisted to EEPROM).
extern CalBand calTable[NUM_BANDS];
extern bool    calValid;   // a valid correction table is loaded

#endif // HARDWARE_H
