// telemetry.cpp
//
// 2024, opencode AI

#include "telemetry.h"
#include "hardware.h"

/**
 * @brief Emit the current state machine state as machine-readable telemetry.
 *
 * The simulator (python/sim_display.py) parses this to redraw the display.
 * Prints a line of the form `@STATE:<name>`.
 * @param state  Null-terminated state string (e.g. "IDLE", "SCANNING").
 */
void emitState(const char* state) {
  Serial.print("@STATE:");
  Serial.println(state);
}

/**
 * @brief Emit the currently selected band as telemetry.
 *
 * Prints `@BAND:<name>` so the simulator can update the header bar.
 */
void emitBand() {
  Serial.print("@BAND:");
  Serial.println(BANDS[bandIndex].name);
}

/**
 * @brief Emit the current display layout as telemetry.
 *
 * Prints `@MODE:curve` or `@MODE:numeric`.
 */
void emitMode() {
  Serial.print("@MODE:");
  Serial.println(displayMode == MODE_CURVE ? "curve" : "numeric");
}

/**
 * @brief Emit external-control telemetry.
 *
 * Prints `@CTRL:external` or `@CTRL:local` so a host app can track who owns
 * the unit and when to expect display redraws.
 */
void emitCtrl() {
  Serial.print("@CTRL:");
  Serial.println(extControl ? "external" : "local");
}

/**
 * @brief Emit the calibration wizard progress line.
 *
 * Prints `@CALPHASE:<n>/<total>` giving the current reference step.
 */
void emitCalPhase() {
  Serial.print("@CALPHASE:");
  Serial.print((int)calPhase + 1);
  Serial.print("/");
  Serial.println(NUM_CAL_PHASES);
}

/**
 * @brief Emit the calibration band/point progress as telemetry.
 *
 * Prints `@CALPROG:band=<b>/<N>,pt=<p>/<P>` so the simulator can draw the
 * per-band / per-point progress bar.
 */
void emitCalProgress() {
  Serial.print("@CALPROG:band=");
  Serial.print((int)calBandIndex + 1);
  Serial.print("/");
  Serial.print(NUM_BANDS);
  Serial.print(",pt=");
  Serial.print((int)calPoint + 1);
  Serial.print("/");
  Serial.println(CAL_PTS_PER_BAND);
}

/**
 * @brief Show a transient on-screen status message.
 *
 * @param msg  String to display (kept as a pointer; must stay valid).
 * @param ms   How long the message is shown, in milliseconds.
 */
void showStatus(const char* msg, uint32_t ms) {
  statusMsg = msg;
  statusMsgUntil = millis() + ms;
}

/**
 * @brief Detect a hung scan/calibration sweep (silent analyzer).
 *
 * A sweep is considered stuck if it is still collecting but has received no
 * data for longer than SCAN_TIMEOUT_MS, so the UI can return to IDLE.
 * @param now  Current millis() value.
 * @return true if the current sweep should be aborted, false otherwise.
 */
bool isSweepStuck(uint32_t now) {
  if (!collecting) return false;
  return (now - sweepStart) > SCAN_TIMEOUT_MS;
}
