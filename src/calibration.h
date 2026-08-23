// calibration.h
//
// The measurement-calibration wizard: sweeps every HF band against a 50 ohm
// reference to build a per-band R/X offset table, verifies against a short and
// an open, persists the table to EEPROM, and applies the stored correction to
// live measurements. The main state machine and the rigexpert parser drive
// this module.
//
// 2024, opencode AI

#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <Arduino.h>
#include "config.h"

// Text describing what the user must connect for the current calPhase.
const char* calPhasePrompt();

// Enter the calibration wizard (phase 1 = 50 ohm, counters reset).
void startCalibrate();

// Begin sweeping the current band against the current reference.
void calBeginBandSweep();

// Store one measurement point from a calibration band sweep.
void calHandlePoint(const Measurement& m);

// Issue a single-frequency retry for a bogus calibration point.
void calRetryPoint(float freqMHz);

// Finalise the current band sweep and advance to the next.
void calFinishBand();

// Advance to the next calibration phase, or finish the wizard.
void calFinishPhase();

// Persist / load the calibration table in EEPROM.
void saveCalibration();
void loadCalibration();

// Apply the stored calibration correction to a measurement (in place).
void applyCalibration(Measurement& m);

#endif // CALIBRATION_H
