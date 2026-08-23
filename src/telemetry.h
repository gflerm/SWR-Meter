// telemetry.h
//
// Machine-readable telemetry emitted on the PC UART. The simulator and the
// calibration walkthrough (python/) parse `@STATE:`, `@BAND:`, `@MODE:`,
// `@CTRL:`, `@CALPHASE:` and `@CALPROG:` lines to drive a mirror UI. Also small
// shared helpers (status overlay text, hung-sweep detection).
//
// 2024, opencode AI

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <Arduino.h>

void emitState(const char* state);
void emitBand();
void emitMode();
void emitCtrl();
void emitCalPhase();
void emitCalProgress();

// Show a transient on-screen status message (kept as a pointer; must stay
// valid until it expires).
void showStatus(const char* msg, uint32_t ms);

// True if the current sweep is still collecting but has received no data for
// longer than SCAN_TIMEOUT_MS.
bool isSweepStuck(uint32_t now);

#endif // TELEMETRY_H
