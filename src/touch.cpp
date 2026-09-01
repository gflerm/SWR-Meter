// touch.cpp
//
// 2024, opencode AI

#include "touch.h"
#include "hardware.h"
#include "config.h"

// The current press action, latched while a finger is held.
static TouchAction activeAction = TOUCH_NONE;
static uint32_t    pressSince    = 0;

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
  // Scan the touch controller for up to 5 points; we only use the first.
  String pts = touch.scan();

  // Parse the first "id,x,y,w,h " token.
  int firstSpace = pts.indexOf(' ');
  String first = (firstSpace > 0) ? pts.substring(0, firstSpace) : pts;
  long id = 0; int x = 0, y = 0;
  int c1 = first.indexOf(',');
  int c2 = (c1 > 0) ? first.indexOf(',', c1 + 1) : -1;
  if (c1 > 0) {
    id = first.substring(0, c1).toInt();
    x  = (c2 > 0) ? first.substring(c1 + 1, c2).toInt() : 0;
    y  = (c2 > 0) ? first.substring(c2 + 1).toInt() : 0;
  }
  // Guard/clamp against the touch panel's native 320x480 addressing.
  if (id < 0 || id == 255) {          // 255,0,0,0,0 = no touch
    activeAction = TOUCH_NONE;
    pressSince = 0;
    return TOUCH_NONE;
  }
  // Remap native touch (320x480 portrait) to our 480x320 landscape.
  // The touch panel is 320 wide x 480 tall natively; after rotating the
  // display 90°, a (tx,ty) maps to landscape (x,y) = (ty, 319 - tx).
  int lx = y;
  int ly = 319 - x;
  if (lx < 0) lx = 0; if (lx >= (int)screenW) lx = screenW - 1;
  if (ly < 0) ly = 0; if (ly >= (int)screenH) ly = screenH - 1;

  TouchAction act = actionAt(lx, ly, screenW, screenH);
  uint32_t now = millis();
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
