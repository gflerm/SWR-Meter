// rigexpert.cpp
//
// 2024, opencode AI

#include "rigexpert.h"
#include "hardware.h"
#include "telemetry.h"
#include "calibration.h"

/**
 * @brief Reject physically impossible R/X readings.
 *
 * @param r  Series resistance (ohm).
 * @param x  Series reactance (ohm).
 * @return false for NaN/Inf, non-positive/mega-ohm resistance, or |X| too
 *         large; otherwise true.
 */
bool isValidReading(float r, float x) {
  if (!isfinite(r) || !isfinite(x)) return false;
  if (r <= 0.0f || r > MAX_RESISTANCE) return false;
  if (fabsf(x) > MAX_REACTANCE) return false;
  return true;
}

/**
 * @brief Compute SWR from series R and X in a Z0 system.
 *
 * @param r  Series resistance (ohm).
 * @param x  Series reactance (ohm).
 * @return The standing wave ratio, or MAX_SWR+1 for the degenerate cases
 *         where the reflection coefficient magnitude approaches 1.
 */
float computeSWR(float r, float x) {
  float num = sqrtf((r - Z0) * (r - Z0) + x * x);
  float den = sqrtf((r + Z0) * (r + Z0) + x * x);
  if (den <= 0.0f) return MAX_SWR + 1.0f; // degenerate -> reject
  float gamma = num / den;
  float d = 1.0f - gamma;
  if (d <= 0.0f) return MAX_SWR + 1.0f;   // |gamma| ~ 1 -> reject
  return (1.0f + gamma) / d;
}

/**
 * @brief Parse a "freqMHz,R,X" CSV line into a Measurement.
 *
 * This is a SYNTACTIC parse: it accepts any finite frequency in the analyzer
 * range and any finite R/X, so that the calibration wizard can collect the
 * extreme readings it needs (a SHORT reads R~0, an OPEN reads very large R/X).
 * Physical plausibility (R > 0, sane SWR) is applied separately for normal
 * scans in processLine().
 *
 * @param line  Null-terminated line; modified in place (commas replaced by
 *              NUL terminators).
 * @param m     Output measurement (filled on success).
 * @return true if the line parsed into finite, in-range fields, else false.
 */
bool parseFRXLine(char* line, Measurement& m) {
  char* p1 = strchr(line, ',');
  if (!p1) return false;
  *p1 = '\0';
  char* p2 = strchr(p1 + 1, ',');
  if (!p2) return false;
  *p2 = '\0';

  m.freqMHz = atof(line);
  m.r       = atof(p1 + 1);
  m.x       = atof(p2 + 1);
  m.swr     = computeSWR(m.r, m.x);

  m.valid = isfinite(m.freqMHz) && isfinite(m.r) && isfinite(m.x)
         && m.freqMHz >= 0.0f
         && m.freqMHz <= 200.0f;      // analyzer range: 0.06 - 170 MHz
  return m.valid;
}

/**
 * @brief Append a measurement to the scan buffer if space remains.
 *
 * Also refreshes sweepStart so a live sweep that streams points slowly never
 * looks "stuck" to the abort watchdog.
 *
 * @param m  Measurement to store.
 */
void storePoint(const Measurement& m) {
  if (scanCount < MAX_POINTS) {
    scanPoints[scanCount++] = m;
  }
  sweepStart = millis();
}

/**
 * @brief Drain the AA-30 UART, assembling bytes into lines.
 *
 * Reads all pending Serial1 bytes, terminates each newline-terminated line,
 * and hands it to processLine(). CR bytes are discarded.
 */
void pollAnalyzer() {
  while (AA_PORT.available()) {
    char c = AA_PORT.read();

    if (c == '\n') {
      lineBuf[lineLen < LINE_BUF - 1 ? lineLen : LINE_BUF - 1] = '\0';
      processLine(lineBuf);
      lineLen = 0;
    } else if (c != '\r') {
      if (lineLen < LINE_BUF - 1) {
        lineBuf[lineLen++] = c;
      } else {
        // Line too long: drop silently (no unbounded growth).
      }
    }
  }
}

/**
 * @brief Dispatch one assembled AA-30 line.
 *
 * Trims whitespace, then:
 *  - A valid `freq,R,X` point  -> routes to calibration or stores the point,
 *    completes the scan when POINTS_PER_SCAN is reached, and emits telemetry.
 *  - `OK`                      -> completes a scan if points were collected.
 *  - Other non-data text       -> echoed as an AA-30 message.
 *  - Data-shaped but invalid   -> reported as a bogus/discarded line.
 *
 * @param line  Null-terminated line to process (modified in place).
 */
void processLine(char* line) {
  // Trim leading/trailing whitespace.
  char* s = line;
  while (*s == ' ' || *s == '\t') s++;
  char* e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
  if (*s == '\0') return;

  // Detect data-shaped lines BEFORE parseFRXLine nulls out the commas.
  bool hasComma = (strchr(s, ',') != NULL);

  Measurement m;
  if (parseFRXLine(s, m)) {
    if (calMeasuring) {
      // Calibration sweep in progress: route the point to the wizard.
      calHandlePoint(m);
      if (calPoint >= CAL_PTS_PER_BAND && !calFinishPending) {
        calFinishBand();
      }
    } else {
      // Normal sweep: apply stored calibration correction, then gate on
      // physical plausibility before storing (parseFRXLine is syntactic).
      applyCalibration(m);
      if (m.valid && isValidReading(m.r, m.x) && m.swr >= 1.0f && m.swr <= MAX_SWR) {
        storePoint(m);
        if (collecting && scanCount >= POINTS_PER_SCAN) {
          collecting = false;
          currentState = STATE_DISPLAYING;
        }
      }
    }
    // Telemetry to PC (6 decimals for MHz preserves narrow-band resolution).
    // Use dtostrf, not snprintf(%f): %f is a no-op when newlib-nano is linked
    // without the float printf object, producing blank output.
    char fs[16], rs[16], xs[16], ss[16];
    dtostrf(m.freqMHz, 0, 6, fs);
    dtostrf(m.r,       0, 1, rs);
    dtostrf(m.x,       0, 1, xs);
    dtostrf(m.swr,     0, 2, ss);
    Serial.print("F="); Serial.print(fs); Serial.print("MHz R=");
    Serial.print(rs);  Serial.print(" X="); Serial.print(xs);
    Serial.print(" SWR="); Serial.println(ss);
    // Machine-readable point for the simulator.
    Serial.print("@POINT:");
    Serial.print(fs); Serial.print(",");
    Serial.print(rs); Serial.print(",");
    Serial.print(xs); Serial.print(",");
    Serial.println(ss);
    if (currentState == STATE_DISPLAYING) {
      emitState("DISPLAYING");
      emitBand();
    }
  } else if (strcasecmp(s, "OK") == 0) {
    Serial.println("<AA-30 OK>");
    if (calMeasuring) {
      // Each band sweep is terminated by an OK. Only finish the band once at
      // least one point was collected (the ON/fq/sw OKs arrive with none).
      // Skip while a single-point retry is in flight (its OKs are not a band
      // boundary).
      if (calPoint > 0 && !calFinishPending && !calRetrying) {
        calFinishBand();
      }
    } else if (collecting && scanCount > 0) {
      collecting = false;
      currentState = STATE_DISPLAYING;
      emitState("DISPLAYING");
      emitBand();
    }
  } else if (!hasComma) {
    // Non-data response (e.g. "AA-30.ZERO 200").
    Serial.print("<AA-30> ");
    Serial.println(s);
  } else {
    // A data-shaped line that failed validation.
    if (calMeasuring && !calRetrying) {
      // Extract the frequency for a one-shot retry so a single bogus point
      // doesn't short-change the band.
      char* comma = strchr(s, ',');
      if (comma) {
        *comma = '\0';
        float freq = atof(s);
        Serial.print("<AA-30 BOGUS, retrying @ ");
        Serial.print(freq);
        Serial.println("> ");
        calRetryPoint(freq);
      } else {
        Serial.print("<AA-30 BOGUS, discarded> ");
        Serial.println(s);
      }
    } else {
      Serial.print("<AA-30 BOGUS, discarded> ");
      Serial.println(s);
    }
  }
}

/**
 * @brief Command the AA-30 to sweep the currently selected band.
 *
 * Enters STATE_SCANNING, resets the point buffer, then powers the RF board
 * (`ON`) and issues `fq`/`sw`/`frx` for the selected band. The streamed
 * points are collected asynchronously by pollAnalyzer() -> processLine().
 */
void startScan() {
  scanCount    = 0;
  collecting   = true;
  sweepStart   = millis();
  currentState = STATE_SCANNING;

  const Band& b = BANDS[bandIndex];
  uint32_t center = (b.low + b.high) / 2;
  uint32_t span   = b.high - b.low;

  emitState("SCANNING");
  emitBand();

  Serial.print("Scanning ");
  Serial.print(b.name);
  Serial.print("  center=");
  Serial.print(center);
  Serial.print(" Hz  span=");
  Serial.print(span);
  Serial.println(" Hz");

  // Power the RF board first; without ON the AA-30 returns no measurement data.
  // The analyzer needs a generous gap between commands (esp. before frx) or it
  // drops the sweep request when the setup commands arrive back-to-back.
  AA_PORT.println("ON");
  delay(400);
  AA_PORT.print("fq"); AA_PORT.println(center);
  delay(300);
  AA_PORT.print("sw"); AA_PORT.println(span);
  delay(300);
  AA_PORT.print("frx"); AA_PORT.println(POINTS_PER_SCAN - 1);
}
