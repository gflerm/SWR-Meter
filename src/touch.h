// touch.h
//
// DFR0669 GT911 capacitive touch input. Polls the touch controller over I2C and
// exposes a simple tap classifier that maps a pressed location to a UI action
// (START / BAND / MODE / CAL) or "anywhere" (used to advance the welcome page).
//
// 2024, opencode AI

#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>

// UI actions a tap can select.
enum TouchAction {
  TOUCH_NONE,     // no active touch
  TOUCH_ANY,      // a tap anywhere (welcome advance / dismiss)
  TOUCH_START,    // scan / begin sweep
  TOUCH_BAND,     // cycle band
  TOUCH_MODE,     // toggle layout
  TOUCH_CAL       // enter calibration
};

// Poll the touch controller, debounce, and classify the current press.
// zoneWidth/zoneHeight are the current screen dimensions in landscape.
TouchAction touchReadAction(uint16_t screenW, uint16_t screenH);

#endif // TOUCH_H
