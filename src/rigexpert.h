// rigexpert.h
//
// Driver for the RigExpert AA-30.ZERO antenna & cable analyzer on hardware
// UART1 (Serial1). Owns the ASCII line assembler, the `freq,R,X` parser and
// validation, and the scan sweep driver. Calibration points are handed to the
// calibration module; the main state machine is notified via telemetry.
//
// 2024, opencode AI

#ifndef RIGEXPERT_H
#define RIGEXPERT_H

#include <Arduino.h>
#include "config.h"

// Reject physically impossible R/X readings (NaN/Inf or absurd magnitudes).
bool isValidReading(float r, float x);

// Compute SWR from series R and X in a Z0 system.
float computeSWR(float r, float x);

// Parse a "freqMHz,R,X" CSV line into a Measurement (line modified in place).
bool parseFRXLine(char* line, Measurement& m);

// Append a measurement to the scan buffer if space remains.
void storePoint(const Measurement& m);

// Drain the AA-30 UART, assembling bytes into lines.
void pollAnalyzer();

// Dispatch one assembled AA-30 line (trimmed, terminated).
void processLine(char* line);

// Command the AA-30 to sweep the currently selected band.
void startScan();

#endif // RIGEXPERT_H
