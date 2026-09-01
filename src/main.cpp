// main.cpp
//
// UART Bridge between a RigExpert AA-30.ZERO antenna & cable analyzer and an
// Arduino Uno R4 Minima, with a DFRobot DFR0669 3.5" ILI9488 SPI display and a
// GT911 capacitive touchscreen UI.
//
// This file is the orchestrator: it owns setup(), loop(), the UI state
// machine, and PC-command handling. Hard work is delegated to the modules:
//
//   display.*      all DFR0669 (ILI9488) rendering (updateDisplay, draw*)
//   touch.*        GT911 capacitive-touch scan + tap->action classifier
//   rigexpert.*    AA-30 UART polling, ASCII parser, scan driver
//   calibration.*  calibration wizard, EEPROM table, correction
//   telemetry.*    @STATE/@BAND/@MODE/@CTRL/@CALPHASE/@CALPROG emit helpers
//   hardware.*     one shared definition of global state + tft/touch + AA_PORT
//   config.h       pin map, constants, shared data types
//
// The AA-30.ZERO has two UARTs; on the R4 the only reliable path is hardware
// Serial1 (D0 = RX, D1 = TX), which talks to the analyzer's UART1 interface.
// The R4's SoftwareSerial cannot drive the D4/D7 pins the analyzer's default
// UART2 uses (SoftwareSerial.begin() fails), so UART1 + Serial1 is required.
//
// Pin map (no overlaps):
//   AA-30   TX -> D0 (Serial1 RX)     AA-30   RX -> D1 (Serial1 TX)
//   Display SCLK -> D13 (SPI SCK)     Display MOSI -> D11 (SPI COPI/MOSI)
//   Display CS  -> D10                Display DC  -> D9
//   Display RST -> D8                 (BL on by default, not a GPIO)
//   Touch SDA -> A4   Touch SCL -> A5  (GT911 @ 0x5D, INT/RST optional)
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
#include "touch.h"
#include "battery.h"

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
          Serial.print("@BATT:");
          if (batteryPresent) {
            Serial.print((int)batteryPct);
            Serial.print(",");
            Serial.println((int)batteryMv);
          } else {
            Serial.println("none");
          }
        }
        else if (strcmp(pcCmdBuf, "!I2C:SCAN") == 0) {
          Serial.print("@I2C:");
          uint8_t found = 0;
          bool busy = false;
          for (uint8_t a = 0x01; a < 0x7F; a++) {
            Wire.beginTransmission(a);
            uint8_t rc = Wire.endTransmission();
            if (rc == 0) {
              Serial.print("0x");
              Serial.print(a, HEX);
              Serial.print(" ");
              found++;
            } else if (rc == 2) {
              busy = true;   // address NACK'd, but a device held SDA
            }
          }
          if (busy) Serial.print("[bus-busy] ");
          Serial.print("(count=");
          Serial.print((int)found);
          Serial.println(")");
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
 * @brief One-time initialisation: UARTs, EEPROM, display, touch, battery and
 *        boot page.
 */
void setup() {
  AA_PORT.begin(38400);   // AA-30 analyzer UART1
  Serial.begin(PC_BAUD);  // PC console / telemetry

  EEPROM.begin();         // calibration persistence
  loadCalibration();

  tft.begin();
  tft.setRotation(1);              // landscape: 480x320
  tft.fillScreen(COLOR_RGB565_BLACK);

  touch.begin();                   // GT911 capacitive touch (I2C)

  // LiPo fuel gauge (MAX17043) shares the same I2C bus (address 0x36).
  pinMode(BAT_ALERT_PIN, INPUT_PULLUP);   // active-low low-battery alert
  if (batteryBegin() == 0) {
    batteryPresent = true;
    batterySetAlert(BAT_LOW_PCT);
    batteryPct = batteryPercent();
    batteryMv  = batteryVoltageMv();
    Serial.print("Battery gauge OK: ");
    Serial.print(batteryPct); Serial.print("%  ");
    Serial.print(batteryMv); Serial.println(" mV");
  } else {
    batteryPresent = false;
    Serial.println("No battery gauge (LiPower shield not detected).");
  }

  displayWelcome();
  emitState("WELCOME");
}

/**
 * @brief One Arduino main loop iteration: read touch, step the state machine,
 *        and drain the analyzer UART.
 */
void loop() {
  static uint32_t lastBattery = 0;
  uint32_t now = millis();

  // Refresh the fuel gauge ~2x/sec (throttled I2C read; cheap).
  if (batteryPresent && (now - lastBattery) >= 500) {
    lastBattery = now;
    batteryPct = batteryPercent();
    batteryMv  = batteryVoltageMv();
  }

  // Low-battery warning: flash a status message while the alert is active or
  // the gauge reads at/below the threshold.
  if (batteryPresent) {
    bool low = batteryLowAlertActive() || batteryPct <= BAT_LOW_PCT;
    if (low && statusMsg == NULL) {
      showStatus("LOW BATTERY", 1500);
    }
  }

  // Poll touch and translate the current press into one-shot button edges.
  TouchAction act = touchReadAction(TFT_W, TFT_H);
  bool startPressed = false, bandPressed = false;
  bool modePressed  = false, calPressed  = false;
  switch (act) {
    case TOUCH_ANY:   startPressed = bandPressed = modePressed = calPressed = true; break;
    case TOUCH_START: startPressed = true; break;
    case TOUCH_BAND:  bandPressed  = true; break;
    case TOUCH_MODE:  modePressed  = true; break;
    case TOUCH_CAL:   calPressed   = true; break;
    default: break;   // TOUCH_NONE
  }

  // PC soft-button commands + passthrough (lines starting with '!').
  handlePcCommands(startPressed, bandPressed, modePressed, calPressed);

  // Abort a genuinely hung sweep (silent analyzer) so the unit stays
  // responsive. A press never aborts a sweep: the START tap that begins a
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
