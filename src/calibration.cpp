// calibration.cpp
//
// 2024, opencode AI

#include <EEPROM.h>
#include "calibration.h"
#include "hardware.h"
#include "telemetry.h"
#include "rigexpert.h"

/**
 * @brief Text describing what the user must connect for the current phase.
 *
 * @return Pointer to a static string for the current calPhase: a 50 ohm load,
 *         a short, or an open.
 */
/**
 * @brief Text describing what the user must connect for the current phase.
 *
 * Single-phase wizard: always the 50 ohm reference.
 *
 * @return "Connect 50 ohm load".
 */
const char* calPhasePrompt() {
  (void)calPhase;   // phase is fixed at CAL_PHASE_50 in the single-phase wizard
  return "Connect 50 ohm load";
}

/**
 * @brief Enter the calibration wizard.
 *
 * Initialises the wizard to its first phase (50 ohm reference), resets all
 * calibration counters, and transitions to STATE_CALIBRATE. The user then
 * presses START to begin sweeping, MODE to cancel.
 */
void startCalibrate() {
  calActive    = true;
  calMeasuring = false;
  calDone      = false;
  calPhase     = CAL_PHASE_50;
  calBandIndex = 0;
  calPoint     = 0;
  calTotalPass = 0;
  calFailCount = 0;
  scanCount    = 0;
  currentState = STATE_CALIBRATE;
  emitState("CALIBRATE");
  emitCalPhase();
  Serial.println("=== CALIBRATION WIZARD ===");
  Serial.println(calPhasePrompt());
  Serial.println("Press START to begin, MODE to exit.");
}

/**
 * @brief Begin sweeping the current band against the current reference.
 *
 * Sends `ON`, `fq`, `sw` and `frx(CAL_PTS_PER_BAND-1)` for the band at
 * calBandIndex. Points are collected asynchronously by calHandlePoint().
 */
void calBeginBandSweep() {
  const Band& b = BANDS[calBandIndex];
  uint32_t center = (b.low + b.high) / 2;
  uint32_t span   = b.high - b.low;

  calPoint     = 0;
  calMeasuring = true;
  calFinishPending = false;
  collecting   = true;
  sweepStart   = millis();

  emitCalProgress();
  Serial.print("Cal sweep band ");
  Serial.print(calBandIndex + 1);
  Serial.print("/");
  Serial.print(NUM_BANDS);
  Serial.print(" (");
  Serial.print(b.name);
  Serial.print(") ref=");
  Serial.println(calPhasePrompt());

  // Power the RF board first; without ON the AA-30 returns no measurement data.
  // Generous gaps prevent the analyzer dropping the sweep request.
  AA_PORT.println("ON");
  delay(400);
  AA_PORT.print("fq"); AA_PORT.println(center);
  delay(300);
  AA_PORT.print("sw"); AA_PORT.println(span);
  delay(300);
  AA_PORT.print("frx"); AA_PORT.println(CAL_PTS_PER_BAND - 1);
}

/**
 * @brief Store one measurement point from a calibration band sweep.
 *
 * Stores the additive correction (50 - R, 0 - X) for the 50 ohm reference,
 * advances the per-band point counter, and emits progress telemetry.
 *
 * @param m  The parsed, validated measurement point.
 */
void calHandlePoint(const Measurement& m) {
  if (!m.valid) return;

  calRetrying = false;   // a single-point retry (if any) succeeded
  CalBand& cb = calTable[calBandIndex];
  if (calPoint < CAL_PTS_PER_BAND) {
    cb.pts[calPoint].freqMHz = m.freqMHz;
    cb.pts[calPoint].rCorr = 50.0f - m.r;
    cb.pts[calPoint].xCorr = 0.0f - m.x;
    cb.count = calPoint + 1;
    calPoint++;
  }
  sweepStart = millis();   // progress received -> a sweep in motion is not stuck
  emitCalProgress();
}

/**
 * @brief Issue a single-frequency retry for a bogus calibration point.
 *
 * The AA-30 streams a whole band in one frx burst; occasionally one point is
 * malformed. A retry commands fq<freq> + sw0 + frx0 to re-measure that single
 * frequency, replaces the point, and lets the band continue. If the retry is
 * also bogus the point is dropped and the sweep resumes.
 *
 * @param freqMHz  The frequency (MHz) of the point that failed validation.
 */
void calRetryPoint(float freqMHz) {
  if (calRetrying) return;   // retry already in flight; don't stack another
  calRetrying = true;
  calRetryFreq = freqMHz;
  uint32_t hz = (uint32_t)(freqMHz * 1e6f);
  Serial.print("Retry cal point @ "); Serial.println(freqMHz);
  AA_PORT.print("fq"); AA_PORT.println(hz);
  AA_PORT.println("sw0");
  AA_PORT.println("frx0");
}

/**
 * @brief Finalise the current band sweep and advance to the next.
 *
 * Marks the band valid (50 ohm phase), evaluates pass/fail for the short/open
 * phases, then either sweeps the next band or completes the phase when all
 * bands are done.
 */
void calFinishBand() {
  if (calFinishPending) return;   // already finishing this band
  calFinishPending = true;
  CalBand& cb = calTable[calBandIndex];
  bool ok = false;

  // Single-phase (50 ohm) band evaluation: a band is calibrated if it
  // collected a usable number of points (we key the band's completion on the
  // trailing OK, so a couple of bogus points may be dropped). Judge pass by
  // the FRACTION of points whose R error is within 10% of 50 ohm, so a few
  // outliers don't fail the band.
  cb.valid = (cb.count >= CAL_PTS_PER_BAND / 2);
  if (cb.valid) {
    float good = 0.0f;
    for (uint8_t i = 0; i < cb.count; i++) {
      if (fabsf(cb.pts[i].rCorr) / 50.0f * 100.0f <= 10.0f) {
        good += 1.0f;
      }
    }
    ok = (good / cb.count * 100.0f >= (float)CAL_PASS_PCT);
  } else {
    cb.valid = false;   // too few good points
  }

  if (ok) calTotalPass++;
  else    calFailCount++;

  // Live per-band pass/fail telemetry for the guided walkthrough.
  Serial.print("@CALBAND:band=");
  Serial.print((int)calBandIndex + 1);
  Serial.print(",pass=");
  Serial.println(ok ? "1" : "0");
  Serial.print(calPhasePrompt());
  Serial.print(" band ");
  Serial.print(BANDS[calBandIndex].name);
  Serial.print(": ");
  Serial.println(ok ? "PASS" : "FAIL");

  calBandIndex++;
  if (calBandIndex < NUM_BANDS) {
    calBeginBandSweep();
  } else {
    calFinishPhase();
  }
}

/**
 * @brief Finish the calibration wizard.
 *
 * Persists the 50 ohm correction table (only if at least one band passed, so
 * a wrong-load run can't overwrite good data), then sets the overall
 * pass/fail result and enters STATE_CAL_DONE.
 */
void calFinishPhase() {
  calMeasuring = false;
  collecting   = false;
  calBandIndex = 0;
  calPoint     = 0;

  if (calPhase == CAL_PHASE_50) {
    // Persist the 50 ohm correction table only if it is genuinely usable.
    // A wrong load connected here (e.g. a short) will fail every band; saving
    // that would overwrite a previously good table with garbage.
    if (calTotalPass > 0) {
      saveCalibration();
    } else {
      calValid = false;
      Serial.println("50 ohm phase: no bands calibrated; leaving existing table untouched.");
    }
    Serial.print("50 ohm phase: ");
    Serial.print(calTotalPass);
    Serial.print("/");
    Serial.print(NUM_BANDS);
    Serial.println(" bands within tolerance.");
  }

  // Single-phase wizard: sweep done -> show the result summary.
  calDone   = true;
  calPassed = (calFailCount == 0);
  currentState = STATE_CAL_DONE;
  emitState("CAL_DONE");
  Serial.print("@CALRESULT:");
  Serial.println(calPassed ? "PASS" : "FAIL");
  Serial.print("CALIBRATION ");
  Serial.println(calPassed ? "PASSED" : "FAILED");
}

// ---- EEPROM persistence ----------------------------------------------

/**
 * @brief Persist the calibration table to EEPROM.
 *
 * Writes a 4-byte magic marker followed by, for each band, a validity byte and
 * CAL_PTS_PER_BAND entries of (freqMHz, rCorr, xCorr) floats.
 */
void saveCalibration() {
  int addr = CAL_EEPROM_ADDR;
  EEPROM.write(addr++, (CAL_EEPROM_MAGIC >> 24) & 0xFF);
  EEPROM.write(addr++, (CAL_EEPROM_MAGIC >> 16) & 0xFF);
  EEPROM.write(addr++, (CAL_EEPROM_MAGIC >> 8) & 0xFF);
  EEPROM.write(addr++, CAL_EEPROM_MAGIC & 0xFF);

  for (uint8_t b = 0; b < NUM_BANDS; b++) {
    const CalBand& cb = calTable[b];
    EEPROM.write(addr++, cb.valid ? 1 : 0);
    for (uint8_t i = 0; i < CAL_PTS_PER_BAND; i++) {
      const CalPoint& p = cb.pts[i];
      EEPROM.put(addr, p.freqMHz); addr += sizeof(float);
      EEPROM.put(addr, p.rCorr);   addr += sizeof(float);
      EEPROM.put(addr, p.xCorr);   addr += sizeof(float);
    }
  }
  Serial.println("Calibration table saved to EEPROM.");
}

/**
 * @brief Load the calibration table from EEPROM.
 *
 * Validates the 4-byte magic marker. If present, reads each band's validity
 * byte and CAL_PTS_PER_BAND correction entries, and sets calValid if any band
 * is valid. Silently marks calValid=false on a fresh/empty EEPROM.
 */
void loadCalibration() {
  int addr = CAL_EEPROM_ADDR;
  uint32_t magic = 0;
  magic |= (uint32_t)EEPROM.read(addr++) << 24;
  magic |= (uint32_t)EEPROM.read(addr++) << 16;
  magic |= (uint32_t)EEPROM.read(addr++) << 8;
  magic |= (uint32_t)EEPROM.read(addr++);

  calValid = false;
  if (magic != CAL_EEPROM_MAGIC) {
    Serial.println("No calibration table in EEPROM (fresh unit).");
    return;
  }

  for (uint8_t b = 0; b < NUM_BANDS; b++) {
    CalBand& cb = calTable[b];
    cb.valid = EEPROM.read(addr++) != 0;
    cb.count = CAL_PTS_PER_BAND;
    for (uint8_t i = 0; i < CAL_PTS_PER_BAND; i++) {
      CalPoint& p = cb.pts[i];
      EEPROM.get(addr, p.freqMHz); addr += sizeof(float);
      EEPROM.get(addr, p.rCorr);   addr += sizeof(float);
      EEPROM.get(addr, p.xCorr);   addr += sizeof(float);
    }
    if (cb.valid) calValid = true;
  }
  Serial.println(calValid ? "Calibration loaded from EEPROM." : "Calibration table empty.");
}

/**
 * @brief Apply the stored calibration correction to a measurement.
 *
 * Finds the band containing m.freqMHz, then linearly interpolates the stored
 * R/X offsets between the two bracketing calibration points (removing table
 * "steps"). Clamps to the band's first/last point outside the table range.
 * No-op if no valid calibration exists or the measurement is already invalid.
 *
 * @param m  The measurement to correct (modified in place).
 */
void applyCalibration(Measurement& m) {
  if (!calValid || !m.valid) return;

  // Find the band containing this frequency.
  for (uint8_t b = 0; b < NUM_BANDS; b++) {
    if (!calTable[b].valid) continue;
    float lo = BANDS[b].low / 1e6f;
    float hi = BANDS[b].high / 1e6f;
    if (m.freqMHz < lo - 0.01f || m.freqMHz > hi + 0.01f) continue;

    const CalBand& cb = calTable[b];
    if (cb.count == 0) return;

    // Locate the two points bracketing m.freqMHz.
    uint8_t idx = 0;
    while (idx + 1 < cb.count && cb.pts[idx + 1].freqMHz <= m.freqMHz) idx++;

    // Interpolate between pts[idx] and pts[idx+1]; clamp outside the table.
    float f0 = cb.pts[idx].freqMHz;
    bool  hasNext = (idx + 1 < cb.count);
    float f1 = hasNext ? cb.pts[idx + 1].freqMHz : f0;
    float t  = 0.0f;
    if (hasNext && f1 > f0) {
      t = (m.freqMHz - f0) / (f1 - f0);
      if (t < 0.0f) t = 0.0f;
      if (t > 1.0f) t = 1.0f;
    }
    float rCorr = hasNext ? (cb.pts[idx].rCorr + t * (cb.pts[idx + 1].rCorr - cb.pts[idx].rCorr))
                          : cb.pts[idx].rCorr;
    float xCorr = hasNext ? (cb.pts[idx].xCorr + t * (cb.pts[idx + 1].xCorr - cb.pts[idx].xCorr))
                          : cb.pts[idx].xCorr;

    m.r += rCorr;
    m.x += xCorr;
    m.swr = computeSWR(m.r, m.x);
    m.valid = isValidReading(m.r, m.x) && m.swr >= 1.0f && m.swr <= MAX_SWR;
    return;
  }
}
