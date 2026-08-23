// main.cpp
//
// UART Bridge between a RigExpert AA-30.ZERO antenna & cable analyzer and an
// Arduino Uno R4 Minima, with a Waveshare 2.4" ILI9341 SPI display and four
// push-button controls.
//
// This file is the orchestrator: it owns setup(), loop(), the UI state
// machine, and PC-command handling. Hard work is delegated to the modules:
//
//   display.*      all ILI9341 rendering (updateDisplay, draw*)
//   rigexpert.*    AA-30 UART polling, ASCII parser, scan driver
//   calibration.*  calibration wizard, EEPROM table, correction
//   telemetry.*    @STATE/@BAND/@MODE/@CTRL/@CALPHASE/@CALPROG emit helpers
//   hardware.*     one shared definition of global state + tft + AA_PORT
//   config.h       pin map, constants, shared data types
//
// The AA-30.ZERO has two UARTs; on the R4 the only reliable path is hardware
// Serial1 (D0 = RX, D1 = TX), which talks to the analyzer's UART1 interface.
// The R4's SoftwareSerial cannot drive the D4/D7 pins the analyzer's default
// UART2 uses (SoftwareSerial.begin() fails), so UART1 + Serial1 is required.
//
// Pin map (no overlaps):
//   AA-30   TX -> D0 (Serial1 RX)     AA-30   RX -> D1 (Serial1 TX)
//   Display CLK -> D13 (SPI SCK)      Display DIN -> D11 (SPI COPI/MOSI)
//   Display CS  -> D10                Display DC  -> D9
//   Display RST -> D8                 (BL wired to 3.3 V, not a GPIO)
//   START -> D2   BAND -> D3   MODE -> D4   CAL -> D5   (INPUT_PULLUP, active-low)
//
// 2024, opencode AI

#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"
#include "hardware.h"
#include "telemetry.h"
#include "display.h"
#include "rigexpert.h"
#include "calibration.h"

// ======================================================================
// LOCAL (non-shared) STATE
// ======================================================================

/**
 * @brief Emit the correct state string for !GET:STATE query.
 */
static const char* stateString() {
  switch (currentState) {
    case STATE_WELCOME:    return "WELCOME";
    case STATE_IDLE:       return "IDLE";
    case STATE_CALIBRATE:  return "CALIBRATE";
    case STATE_CAL_DONE:   return "CAL_DONE";
    case STATE_SCANNING:   return "SCANNING";
    default:               return "DISPLAYING";
  }
}

// ======================================================================
// PC TELEMETRY + COMMANDS
// ======================================================================

/**
 * @brief Process PC serial bytes (soft buttons + analyzer passthrough).
 *
 * Reads all pending USB-CDC bytes. Complete lines starting with `!` are
 * treated as soft-button / query commands and set the matching button flag
 * (preventing them from being forwarded). Any other line, while in IDLE, is
 * forwarded verbatim to the AA-30 (Serial1) for direct control.
 *
 * @param s  Set true if `!BTN:START` received.
 * @param b  Set true if `!BTN:BAND`  received.
 * @param m  Set true if `!BTN:MODE`  received.
 * @param c  Set true if `!BTN:CAL`   received.
 */
void handlePcCommands(bool& s, bool& b, bool& m, bool& c) {
  while (Serial.available()) {
    char ch = (char)Serial.read();

    if (ch == '\n') {
      pcCmdBuf[pcCmdLen < LINE_BUF - 1 ? pcCmdLen : LINE_BUF - 1] = '\0';
      if (pcCmdBuf[0] == '!') {
        // Soft-button command.
        if      (strcmp(pcCmdBuf, "!BTN:START") == 0) s = true;
        else if (strcmp(pcCmdBuf, "!BTN:BAND")  == 0) b = true;
        else if (strcmp(pcCmdBuf, "!BTN:MODE")  == 0) m = true;
        else if (strcmp(pcCmdBuf, "!BTN:CAL")   == 0) c = true;
        else if (strcmp(pcCmdBuf, "!CTRL:EXTERNAL") == 0) {
          extControl = true;
          extSplashDone = false;
          emitCtrl();
        }
        else if (strcmp(pcCmdBuf, "!CTRL:LOCAL") == 0) {
          extControl = false;
          extSplashDone = false;
          emitCtrl();
        }
        else if (strcmp(pcCmdBuf, "!GET:STATE") == 0) {
          emitState(stateString());
          emitBand();
          emitMode();
          emitCtrl();
        }
      } else if (currentState == STATE_IDLE) {
        // Pass non-command line through to the analyzer.
        AA_PORT.write(pcCmdBuf, strlen(pcCmdBuf));
        AA_PORT.write('\n');
      }
      pcCmdLen = 0;
    } else if (ch != '\r') {
      if (pcCmdLen < LINE_BUF - 1) pcCmdBuf[pcCmdLen++] = ch;
    }
  }
}

// ======================================================================
// STATE MACHINE
// ======================================================================

/**
 * @brief Dispatch the main UI state machine.
 *
 * Runs one step of the finite state machine each loop based on the button
 * flags. States: WELCOME, IDLE, CALIBRATE, SCANNING, DISPLAYING, CAL_DONE.
 * Reads the band/display-mode/scan/calibrate actions for each state.
 *
 * @param startPressed  True if START was pressed this cycle.
 * @param bandPressed   True if BAND  was pressed this cycle.
 * @param modePressed   True if MODE  was pressed this cycle.
 * @param calPressed    True if CAL   was pressed this cycle.
 */
void handleStateMachine(bool startPressed, bool bandPressed, bool modePressed, bool calPressed) {
  switch (currentState) {
    case STATE_WELCOME:
      // Boot-up instructions page: any button advances to IDLE.
      drawWelcome();
      if (startPressed || bandPressed || modePressed || calPressed) {
        currentState = STATE_IDLE;
        emitState("IDLE");
        emitBand();
        emitMode();
        updateDisplay();
      }
      break;

    case STATE_IDLE:
      // BAND cycles the selected HF band.
      if (bandPressed) {
        bandIndex = (bandIndex + 1) % NUM_BANDS;
        emitBand();
        Serial.print("Band selected: ");
        Serial.println(BANDS[bandIndex].name);
      }
      // MODE toggles the display layout.
      if (modePressed) {
        displayMode = (displayMode == MODE_CURVE) ? MODE_NUMERIC : MODE_CURVE;
        emitMode();
        Serial.print("Display mode: ");
        Serial.println(displayMode == MODE_CURVE ? "curve" : "numeric");
      }
      // START runs a scan of the current band.
      if (startPressed) {
        startScan();
      }
      // CAL enters the calibration / performance check.
      if (calPressed) {
        startCalibrate();
      }
      updateDisplay();
      break;

    case STATE_CALIBRATE:
      // While measuring we just show progress; buttons are ignored until done.
      if (calMeasuring) {
        updateDisplay();
        break;
      }
      // MODE cancels back to IDLE.
      if (modePressed) {
        calActive = false;
        currentState = STATE_IDLE;
      }
      // START begins (or resumes) the current phase's band sweep.
      if (startPressed) {
        calBeginBandSweep();
      }
      updateDisplay();
      break;

    case STATE_CAL_DONE:
      // Summary screen: START or any button returns to IDLE.
      drawCalDone();
      if (startPressed || bandPressed || modePressed || calPressed) {
        calActive = false;
        currentState = STATE_IDLE;
        updateDisplay();
      }
      break;

    case STATE_SCANNING:
      // pollAnalyzer() turns the trailing "OK" into STATE_DISPLAYING.
      updateDisplay();
      break;

    case STATE_DISPLAYING:
      updateDisplay();
      // MODE toggles layout here too.
      if (modePressed) {
        displayMode = (displayMode == MODE_CURVE) ? MODE_NUMERIC : MODE_CURVE;
      }
      if (startPressed) {
        currentState = STATE_IDLE;
      }
      break;
  }
}

// ======================================================================
// SETUP / LOOP
// ======================================================================

/**
 * @brief One-time initialisation: pins, UARTs, EEPROM, display and boot page.
 */
void setup() {
  pinMode(START_PIN, INPUT_PULLUP);
  pinMode(BAND_PIN,  INPUT_PULLUP);
  pinMode(MODE_PIN,  INPUT_PULLUP);
  pinMode(CAL_PIN,   INPUT_PULLUP);

  AA_PORT.begin(38400);   // AA-30 analyzer UART1
  Serial.begin(PC_BAUD);  // PC console / telemetry

  EEPROM.begin();         // calibration persistence
  loadCalibration();

  tft.begin();
  tft.setRotation(1);            // landscape (320x240)
  tft.fillScreen(ILI9341_BLACK);

  displayWelcome();
  emitState("WELCOME");
}

/**
 * @brief One Arduino main loop iteration: read buttons, step the state
 *        machine, and drain the analyzer UART.
 */
void loop() {
  static uint32_t lastDebounce = 0;
  uint32_t now = millis();

  bool startPressed = false, bandPressed = false, modePressed = false, calPressed = false;
  if (now - lastDebounce >= DEBOUNCE_MS) {
    // Active-low with INPUT_PULLUP: pressed means LOW.
    startPressed = digitalRead(START_PIN) == LOW;
    bandPressed  = digitalRead(BAND_PIN)  == LOW;
    modePressed  = digitalRead(MODE_PIN)  == LOW;
    calPressed   = digitalRead(CAL_PIN)   == LOW;
    lastDebounce = now;
  }

  // PC soft-button commands + passthrough (lines starting with '!').
  handlePcCommands(startPressed, bandPressed, modePressed, calPressed);

  // Abort a genuinely hung sweep (silent analyzer) so the unit stays
  // responsive. Buttons never abort a sweep: the START press that begins a
  // sweep would otherwise immediately abort it, and a live sweep updates
  // sweepStart on every point so it never appears stuck while progressing.
  if (isSweepStuck(now)) {
    if (currentState == STATE_SCANNING ||
        (currentState == STATE_CALIBRATE && calMeasuring)) {
      collecting   = false;
      calMeasuring = false;
      currentState = STATE_IDLE;
      showStatus("Aborted", 1500);
      emitState("IDLE");
      emitBand();
      emitMode();
    }
  }

  handleStateMachine(startPressed, bandPressed, modePressed, calPressed);

  // Always absorb any analyzer bytes into the line buffer; in scan mode the
  // completion of the stream (a trailing "OK") drives the state transition.
  pollAnalyzer();

  delay(2);
}
