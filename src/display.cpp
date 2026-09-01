// display.cpp
//
// 2024, opencode AI

#include "display.h"
#include "hardware.h"
#include "calibration.h"

// Color aliases (TFT_eSPI RGB565 constants) for readability.
#define COLOR_BLACK   TFT_BLACK
#define COLOR_WHITE   TFT_WHITE
#define COLOR_BLUE    TFT_BLUE
#define COLOR_YELLOW  TFT_YELLOW
#define COLOR_GREEN   TFT_GREEN
#define COLOR_CYAN    TFT_CYAN
#define COLOR_ORANGE  TFT_ORANGE
#define COLOR_LGRAY   TFT_LIGHTGREY
#define COLOR_DGRAY   TFT_DARKGREY
#define COLOR_RED     TFT_RED

// Panel is 480x320 landscape (ST7796S native 320x480, rotated 90°).
#define SCR_W TFT_W
#define SCR_H TFT_H

/**
 * @brief Draw the boot welcome / touch-instruction screen.
 *
 * Full-screen page (STATE_WELCOME): title bar plus a colour-coded list of the
 * on-screen touch controls and a "tap screen" hint. Also prints the boot
 * banner on the PC console.
 */
void displayWelcome() {
  tft.fillScreen(COLOR_BLACK);

  // Title block.
  tft.fillRect(0, 0, SCR_W, 44, COLOR_BLUE);
  tft.setTextColor(COLOR_WHITE, COLOR_BLUE);
  tft.setTextSize(2);
  tft.setCursor(16, 8);
  tft.print("SWR METER");
  tft.setTextSize(1);
  tft.setCursor(16, 30);
  tft.print("Uno R4 + AA-30.ZERO");

  // Touch instruction rows.
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setTextSize(1);

  tft.setCursor(16, 58);
  tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
  tft.print("[BAND]");
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(84, 58);
  tft.print("Select HF band (160m-10m)");

  tft.setCursor(16, 84);
  tft.setTextColor(COLOR_GREEN, COLOR_BLACK);
  tft.print("[START]");
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(84, 84);
  tft.print("Scan current band");

  tft.setCursor(16, 110);
  tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
  tft.print("[MODE]");
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(84, 110);
  tft.print("Curve / numeric readout");

  tft.setCursor(16, 136);
  tft.setTextColor(COLOR_ORANGE, COLOR_BLACK);
  tft.print("[CAL]");
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(84, 136);
  tft.print("Calibration check");

  tft.setCursor(16, 172);
  tft.setTextColor(COLOR_LGRAY, COLOR_BLACK);
  tft.print("Tap the screen to continue");

  Serial.println("==========================================================");
  Serial.println("SWR METER BOOTED: Uno R4 Minima + AA-30 Zero + MSP4021");
  Serial.println("[BAND] band  [START] scan  [MODE] layout  [CAL] calibrate");
  Serial.println("==========================================================");
}

/**
 * @brief Re-draw the welcome screen from the state machine.
 *
 * Kept separate from displayWelcome() so the boot-time call in setup() and the
 * STATE_WELCOME state-machine path share the same page. Honours the
 * external-control bypass (one splash, then silently return).
 */
void drawWelcome() {
  if (extControl) {
    if (!extSplashDone) { extSplashDone = true; drawExternalSplash(); }
    return;
  }
  displayWelcome();
}

/**
 * @brief Draw a full-screen splash announcing external control.
 *
 * Shown once when extControl first becomes true; thereafter updateDisplay()
 * returns immediately so the host controls the unit at full speed.
 */
void drawExternalSplash() {
  tft.fillScreen(COLOR_BLACK);
  tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
  tft.setTextSize(2);
  tft.setCursor(30, 90);
  tft.print("EXTERNAL CONTROL");
  tft.setTextColor(COLOR_DGRAY, COLOR_BLACK);
  tft.setTextSize(1);
  tft.setCursor(30, 140);
  tft.print("Host is driving the AA-30");
  tft.setCursor(30, 200);
  tft.print("send `!CTRL:LOCAL` to resume");
}

/**
 * @brief Render the current page chrome and content.
 *
 * Draws the blue header (band + state), then delegates to the calibration
 * screens, the idle prompt, or the curve/numeric readout. In external-control
 * mode a one-shot splash is drawn and all subsequent updates are skipped so
 * the host can drive the unit at full speed.
 */
void updateDisplay() {
  // In external control, the host owns the UI. Show a one-shot splash then
  // skip ALL rendering so the loop stays fast and the analyzer UART is drained
  // without being throttled by slow TFT SPI writes.
  if (extControl) {
    if (!extSplashDone) {
      extSplashDone = true;
      drawExternalSplash();
    }
    return;
  }
  const Band& b = BANDS[bandIndex];

  // Header line: band + state.
  tft.fillRect(0, 0, SCR_W, 24, COLOR_BLUE);
  tft.setTextColor(COLOR_WHITE, COLOR_BLUE);
  tft.setTextSize(1);
  tft.setCursor(4, 6);
  tft.print(b.name);
  tft.setCursor(80, 6);
  switch (currentState) {
    case STATE_WELCOME:    tft.print("WELCOME"); break;
    case STATE_IDLE:       tft.print("IDLE"); break;
    case STATE_CALIBRATE:  tft.print(calMeasuring ? "CAL SWEEP" : "CALIBRATE"); break;
    case STATE_CAL_DONE:   tft.print("CAL SUMMARY"); break;
    case STATE_SCANNING:   tft.print("SCANNING..."); break;
    case STATE_DISPLAYING: tft.print("TAP TO SCAN"); break;
  }
  // Battery readout (top-right), when a gauge is present.
  if (batteryPresent) {
    tft.setCursor(SCR_W - 60, 6);
    tft.setTextColor(batteryPct <= BAT_LOW_PCT ? COLOR_YELLOW : COLOR_WHITE, COLOR_BLUE);
    tft.print((int)batteryPct);
    tft.print("%");
    if (batteryPct <= BAT_LOW_PCT) {
      tft.print("!");   // low-battery marker in the header
    }
    tft.setTextColor(COLOR_WHITE, COLOR_BLUE);
  }

  // Calibration screens take over the page body.
  if (currentState == STATE_CALIBRATE) {
    if (calMeasuring) {
      drawCalProgress();
    } else {
      drawCalPrompt();
    }
    return;
  }
  if (currentState == STATE_CAL_DONE) {
    drawCalDone();
    return;
  }

  if (scanCount == 0) {
    // No data yet -> idle prompt.
    tft.fillRect(0, 26, SCR_W, SCR_H - 26, COLOR_BLACK);
    tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
    tft.setCursor(30, 120);
    tft.setTextSize(2);
    tft.print("Tap [START]");
    tft.setCursor(50, 150);
    tft.print("to scan");
    drawStatusOverlay();
    return;
  }

  if (displayMode == MODE_CURVE) {
    drawCurve(b);
  } else {
    drawNumeric();
  }
  drawStatusOverlay();
}

/**
 * @brief Overlay a transient status message on the current screen.
 *
 * Draws statusMsg (e.g. "Aborted") for a short window. No-op once the message
 * expires or none is set.
 */
void drawStatusOverlay() {
  if (statusMsg == NULL || millis() > statusMsgUntil) return;
  tft.setTextSize(2);
  tft.setTextColor(COLOR_RED, COLOR_BLACK);
  tft.setCursor(80, 60);
  tft.print(statusMsg);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_LGRAY, COLOR_BLACK);
  tft.setCursor(60, 92);
  tft.print("Tap screen...");
}

/**
 * @brief Draw the calibration prompt (which reference to connect).
 */
void drawCalPrompt() {
  tft.fillRect(0, 26, SCR_W, SCR_H - 26, COLOR_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
  tft.setCursor(20, 40);
  tft.print("CALIBRATE");
  tft.setTextSize(1);
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(20, 84);
  tft.print("Step ");
  tft.print((int)calPhase + 1);
  tft.print("/");
  tft.print(NUM_CAL_PHASES);
  tft.setCursor(20, 104);
  tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
  tft.print(calPhasePrompt());
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(20, 128);
  tft.print("Tap START to sweep all bands");
  tft.setCursor(20, 150);
  tft.print("[MODE] cancel");
}

/**
 * @brief Draw the calibration progress (phase, band, point, progress bar).
 */
void drawCalProgress() {
  const Band& b = BANDS[calBandIndex];

  tft.fillRect(0, 26, SCR_W, SCR_H - 26, COLOR_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
  tft.setCursor(20, 40);
  tft.print("Calibrating...");
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(20, 64);
  tft.print("Ref: ");
  tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
  tft.print(calPhasePrompt());
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(20, 90);
  tft.print("Band ");
  tft.print((int)calBandIndex + 1);
  tft.print("/");
  tft.print(NUM_BANDS);
  tft.print("  ");
  tft.print(b.name);
  tft.setCursor(20, 114);
  tft.print("Point ");
  tft.print((int)calPoint + 1);
  tft.print("/");
  tft.print(CAL_PTS_PER_BAND);

  // Overall progress bar (bands done + current band fraction).
  int totalPts = NUM_BANDS * CAL_PTS_PER_BAND;
  int done = calBandIndex * CAL_PTS_PER_BAND + calPoint;
  int bw = 300;
  int bx = 20, by = 150;
  int fill = (int)((float)done / totalPts * bw);
  if (fill > bw) fill = bw;
  tft.drawRect(bx, by, bw, 12, COLOR_WHITE);
  tft.fillRect(bx + 1, by + 1, fill - 1, 10, COLOR_GREEN);
  tft.setCursor(20, 170);
  tft.print((done * 100) / totalPts);
  tft.print(" %  [MODE] cancel");
}

/**
 * @brief Draw the calibration wizard final summary (PASS/FAIL).
 */
void drawCalDone() {
  tft.fillRect(0, 0, SCR_W, SCR_H, COLOR_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(calPassed ? COLOR_GREEN : COLOR_RED, COLOR_BLACK);
  tft.setCursor(20, 40);
  tft.print(calPassed ? "CAL DONE" : "CAL FAILED");
  tft.setTextSize(1);
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(20, 90);
  tft.print("Failures: ");
  tft.print((int)calFailCount);
  tft.setCursor(20, 110);
  tft.print(calValid ? "Correction saved" : "Correction NOT saved");
  tft.setCursor(20, 160);
  tft.print("Tap screen to exit");
}

/**
 * @brief Scale scanPoints[] and draw the SWR-vs-frequency curve.
 *
 * Plots into a fixed 1.0–3.0 SWR window with a SWR=2.0 gridline, colours each
 * segment green/yellow/red by SWR threshold, and prints the minimum SWR.
 *
 * @param b  The band being displayed (used for the x-axis range).
 */
void drawCurve(const Band& b) {
  const int x0 = 8, x1 = SCR_W - 8, y0 = 36, y1 = SCR_H - 24;
  const int plotW = x1 - x0;
  const int plotH = y1 - y0;

  tft.fillRect(x0 - 2, y0 - 8, plotW + 4, plotH + 16, COLOR_BLACK);
  tft.drawRect(x0, y0, plotW, plotH, COLOR_WHITE);

  // Y scaling: fixed 1.0 to 3.0 SWR window for readable comparison.
  const float swrMin = 1.0f, swrMax = 3.0f;

  // Draw SWR=2.0 reference gridline.
  int y2 = y1 - (int)((2.0f - swrMin) / (swrMax - swrMin) * plotH);
  tft.drawLine(x0, y2, x1, y2, COLOR_DGRAY);
  tft.setTextColor(COLOR_DGRAY, COLOR_BLACK);
  tft.setCursor(x1 - 24, y2 - 10);
  tft.print("SWR2");

  uint16_t color = COLOR_GREEN;
  for (uint16_t i = 1; i < scanCount; i++) {
    const Measurement& a = scanPoints[i - 1];
    const Measurement& c = scanPoints[i];

    int ax = x0 + (int)((double)(a.freqMHz - b.low / 1e6) / (double)((b.high - b.low) / 1e6) * plotW);
    int cx = x0 + (int)((double)(c.freqMHz - b.low / 1e6) / (double)((b.high - b.low) / 1e6) * plotW);
    int ay = y1 - (int)((a.swr - swrMin) / (swrMax - swrMin) * plotH);
    int cy = y1 - (int)((c.swr - swrMin) / (swrMax - swrMin) * plotH);

    // Clamp into the plot box so out-of-range SWR cannot draw outside.
    ay = (ay < y0) ? y0 : (ay > y1 ? y1 : ay);
    cy = (cy < y0) ? y0 : (cy > y1 ? y1 : cy);

    // Select color by SWR threshold (green < 1.5, yellow < 2.0, red >= 2.0).
    color = (c.swr >= 2.0f) ? COLOR_RED : (c.swr >= 1.5f ? COLOR_YELLOW : COLOR_GREEN);
    tft.drawLine(ax, ay, cx, cy, color);
  }

  // Footer inside plot: min SWR + frequency.
  float swrMinV = 1e9f; float fMin = 0.0f;
  for (uint16_t i = 0; i < scanCount; i++) {
    if (scanPoints[i].swr < swrMinV) { swrMinV = scanPoints[i].swr; fMin = scanPoints[i].freqMHz; }
  }
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(12, y1 + 6);
  tft.print("MinSWR ");
  tft.print(swrMinV, 2);
  tft.print(" @ ");
  tft.print(fMin, 3);
  tft.print(" MHz");
  tft.setCursor(200, y1 + 6);
  tft.print("n=");
  tft.print(scanCount);
}

/**
 * @brief Draw a large single-value numeric readout of the last measurement.
 */
void drawNumeric() {
  const Measurement& m = scanPoints[scanCount - 1];

  tft.fillRect(0, 26, SCR_W, SCR_H - 26, COLOR_BLACK);

  tft.setTextSize(3);
  tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
  tft.setCursor(10, 40);  tft.print("F ");
  tft.print(m.freqMHz, 3); tft.println(" MHz");

  tft.setTextColor(COLOR_GREEN, COLOR_BLACK);
  tft.setCursor(10, 100);  tft.print("R  ");
  tft.print(m.r, 1);      tft.println(" ohm");

  tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
  tft.setCursor(10, 160); tft.print("X  ");
  tft.print(m.x, 1);      tft.println(" ohm");

  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(10, 220); tft.print("SWR ");
  tft.print(m.swr, 2);
}
