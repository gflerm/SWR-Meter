// touch.cpp
//
// 2024, opencode AI

#include "touch.h"
#include "hardware.h"
#include "config.h"

// TFT_eSPI's getTouch() returns true when a press exceeds this pressure
// threshold (raw Z). Tune for the XPT2046 resistive panel.
#define TOUCH_Z_THRESHOLD 600

// A press is only registered once it has been held this long (debounce).
static TouchAction activeAction = TOUCH_NONE;
static uint32_t    pressSince    = 0;
static uint32_t    lastScan      = 0;
#define TOUCH_SCAN_MS 15   // poll rate so SPI/touch doesn't hog the loop

/**
 * @brief Classify a screen coordinate into a UI action by region.
 *
 * Bottom half of the screen is a row of four touch buttons (left to right):
 *   BAND | START | MODE | CAL
 * Pressing anywhere on the top half is treated as TOUCH_ANY (advance/dismiss).
 *
 * @param x  Touch X (0..screenW-1)
 * @param y  Touch Y (0..screenH-1)
 * @param w  Screen width
 * @param h  Screen height
 * @return the matching TouchAction.
 */
static TouchAction actionAt(int x, int y, uint16_t w, uint16_t h) {
  if (y < h / 2) return TOUCH_ANY;          // upper half: "tap anywhere"
  // Lower half: four equal-width buttons.
  uint16_t col = (uint16_t)x * 4 / w;
  switch (col) {
    case 0: return TOUCH_BAND;
    case 1: return TOUCH_START;
    case 2: return TOUCH_MODE;
    default: return TOUCH_CAL;
  }
}

TouchAction touchReadAction(uint16_t screenW, uint16_t screenH) {
  // Rate-limit the poll so the touch SPI reads don't hog the loop.
  uint32_t now = millis();
  if ((now - lastScan) < TOUCH_SCAN_MS) {
    return TOUCH_NONE;
  }
  lastScan = now;

  // Sample the XPT2046. getTouch() returns true if a press is detected with
  // enough force (raw Z > threshold).
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y, TOUCH_Z_THRESHOLD);
  if (!pressed) {
    activeAction = TOUCH_NONE;
    pressSince = 0;
    return TOUCH_NONE;
  }

  // Clamp into the panel (calibration may return slightly out-of-range).
  if (x >= screenW) x = screenW - 1;
  if (y >= screenH) y = screenH - 1;

  TouchAction act = actionAt(x, y, screenW, screenH);
  if (act != activeAction) {
    activeAction = act;
    pressSince = now;
  }
  // Only report a press once it has been held long enough (debounce), so the
  // loop doesn't fire the action on every poll while the finger is still down.
  if ((now - pressSince) >= TOUCH_DEBOUNCE_MS) {
    return activeAction;
  }
  return TOUCH_NONE;
}
