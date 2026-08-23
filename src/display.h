// display.h
//
// All ILI9341 rendering on the 2.4" SPI display. A single updateDisplay()
// entry point draws the header + page content for the current state, and the
// individual draw*() helpers render the welcome, idle prompt, curve, numeric,
// and calibration screens. Honours the external-control flag.
//
// 2024, opencode AI

#ifndef DISPLAY_H
#define DISPLAY_H

#include "config.h"

void displayWelcome();

// Re-draw the welcome screen (honours external-control bypass).
void drawWelcome();

// Render the current page chrome and content for the active state.
void updateDisplay();

// Transient status overlay (e.g. "Aborted").
void drawStatusOverlay();

void drawExternalSplash();

// Page-screen helpers used by updateDisplay() (defined in display.cpp).
void drawCalPrompt();
void drawCalProgress();
void drawCalDone();
void drawCurve(const Band& b);
void drawNumeric();

#endif // DISPLAY_H
