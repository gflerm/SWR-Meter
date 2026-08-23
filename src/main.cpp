// main.cpp
//
// UART Bridge between a RigExpert AA-30.ZERO antenna & cable analyzer and an
// Arduino Uno R4 Minima, with a Waveshare 2.4" ILI9341 SPI display and four
// push-button controls.
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

// ======================================================================
// HARDWARE DEFINITIONS
// ======================================================================

// --- AA-30 ZERO UART (interface UART1, hardware Serial1: RX = D0, TX = D1)
#define AA_PORT Serial1
// PC console / telemetry port. 115200 keeps AA-30 -> PC from being the
// bottleneck while streaming many measurement points.
#define PC_BAUD 115200

// --- Display (Waveshare 2.4" SPI, ILI9341 320x240). SPI bus is fixed on the
//     R4 (MOSI=11, SCK=13); only CS/DC/RST are free to choose.
#define TFT_CS  10
#define TFT_DC   9
#define TFT_RST  8

// --- Control buttons (INPUT_PULLUP, pressed = LOW)
#define START_PIN 2    // Start a scan of the current band
#define BAND_PIN  3    // Cycle through the HF bands
#define MODE_PIN  4    // Toggle display mode (curve / numeric)
#define CAL_PIN   5    // Run a one-off calibration scan (re-zero the reference)

// --- AA-30 signal analysis
#define Z0 50.0f                 // system impedance for SWR math (default 50 ohm)

// Validation limits for rejecting bogus readings (NaN/Inf or absurd magnitudes).
#define MAX_RESISTANCE 1000000.0f  // ohms
#define MAX_REACTANCE  1000000.0f  // ohms
#define MAX_SWR        100.0f      // physically impossible above this for HF

// Fitted line buffer for AA-30 ASCII output (one CSV line at a time).
#define LINE_BUF 96

// Device-side max points retained for display; reset on every scan.
#define MAX_POINTS 256
#define POINTS_PER_SCAN 100   // points requested per band (fits in MAX_POINTS)

// Button debounce (ms).
#define DEBOUNCE_MS 25

// If a scan or calibration sweep receives no valid data within this time,
// abort back to IDLE so the unit never hangs waiting on a silent analyzer.
#define SCAN_TIMEOUT_MS 8000

// ======================================================================
// INCLUDES
// ======================================================================
#include <Arduino.h>
#include <EEPROM.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ======================================================================
// DATA TYPES
// ======================================================================

struct Measurement {
  float freqMHz; // frequency (MHz)
  float r;       // series resistance (ohm)
  float x;       // series reactance (ohm)
  float swr;     // computed SWR
  bool  valid;   // passed all checks
};

// Result of a calibration / performance-check measurement.
struct CalResult {
  bool         valid;
  float        r, x, swr;
  float        devPct;   // % deviation from the reference (resistive refs)
  bool         pass;
  const char*  refName;
};

enum SystemState {
  STATE_WELCOME,
  STATE_IDLE,
  STATE_CALIBRATE,
  STATE_CAL_DONE,
  STATE_SCANNING,
  STATE_DISPLAYING
};

enum DisplayMode {
  MODE_CURVE,   // SWR vs frequency graph
  MODE_NUMERIC  // big R / X / SWR numbers
};

// IARU Region 1 HF amateur band plan (band name, low edge Hz, high edge Hz).
typedef struct {
  const char* name;
  uint32_t    low;
  uint32_t    high;
} Band;

const Band BANDS[] = {
  { "160m",  1810000,  2000000 },
  { "80m",   3500000,  3800000 },
  { "60m",   5351500,  5366500 },
  { "40m",   7000000,  7200000 },
  { "30m",  10100000, 10150000 },
  { "20m",  14000000, 14350000 },
  { "17m",  18068000, 18168000 },
  { "15m",  21000000, 21450000 },
  { "12m",  24890000, 24990000 },
  { "10m",  28000000, 29700000 },
};

#define NUM_BANDS (sizeof(BANDS) / sizeof(BANDS[0]))

// Reference loads for the CALIBRATE performance check.
typedef struct {
  const char* name;
  float       rExp;     // expected series R for resistive references
  float       tolPct;   // +/- tolerance (% of rExp) for pass/fail
  bool        isOpen;   // high-impedance reference (open)
  bool        isShort;  // near-zero reference (short)
} CalRef;

const CalRef CAL_REFS[] = {
  { "50 ohm",   50.0f, 10.0f, false, false },
  { "25 ohm",   25.0f, 10.0f, false, false },
  { "75 ohm",   75.0f, 10.0f, false, false },
  { "100 ohm", 100.0f, 10.0f, false, false },
  { "OPEN",      0.0f,  0.0f,  true, false },
  { "SHORT",     0.0f,  0.0f, false,  true },
};
#define NUM_CAL_REFS (sizeof(CAL_REFS) / sizeof(CAL_REFS[0]))

// ---- Calibration wizard / correction table ---------------------------
// The wizard sweeps every HF band against reference loads, then stores a
// per-band per-point R/X offset table in EEPROM. Normal sweeps apply the
// nearest stored offset to correct the analyzer readings.
#define CAL_PTS_PER_BAND 20   // sweep points per band during calibration
#define CAL_EEPROM_MAGIC 0x4C4E31  // "CAL1" marker
#define CAL_EEPROM_ADDR  0

// One correction entry: an additive offset to apply at a given frequency.
typedef struct {
  float freqMHz;  // measurement frequency (MHz)
  float rCorr;    // add to measured R to get corrected R
  float xCorr;    // add to measured X to get corrected X
} CalPoint;

// Correction table for one band (CAL_PTS_PER_BAND entries).
typedef struct {
  bool     valid;
  uint8_t  count;
  CalPoint pts[CAL_PTS_PER_BAND];
} CalBand;

// Calibration wizard phases (in order).
enum CalPhase {
  CAL_PHASE_50,     // sweep 50 ohm -> build offsets
  CAL_PHASE_SHORT,  // verify with a short
  CAL_PHASE_OPEN    // verify with an open
};
#define NUM_CAL_PHASES 3

// ======================================================================
// GLOBALS
// ======================================================================

SystemState  currentState = STATE_WELCOME;
DisplayMode  displayMode  = MODE_CURVE;
uint8_t      bandIndex    = 0;

Measurement scanPoints[MAX_POINTS];
uint16_t    scanCount     = 0;

// AA-30 line assembler (fixed buffer, no String / heap use).
char      lineBuf[LINE_BUF];
uint8_t   lineLen = 0;
bool      collecting = false;  // true while awaiting frx data

// Timestamp when the last scan/calibration sweep started (for timeout).
uint32_t  sweepStart = 0;

// Transient on-screen message (e.g. "Aborted") shown briefly after an event.
const char* statusMsg = NULL;
uint32_t    statusMsgUntil = 0;

// PC command line assembler (lines starting with '!' are device commands).
char      pcCmdBuf[LINE_BUF];
uint8_t   pcCmdLen = 0;

// Calibration / performance-check state.
uint8_t    calPhase     = CAL_PHASE_50;   // current wizard phase
uint8_t    calBandIndex = 0;              // band currently being swept
uint8_t    calPoint     = 0;              // points collected for current band
bool       calActive    = false;          // wizard active
bool       calMeasuring = false;          // a sweep is in progress
bool       calDone      = false;          // wizard finished (summary shown)
bool       calPassed    = false;          // overall pass/fail of the wizard
uint8_t    calFailCount = 0;              // bands that failed (per phase)
uint8_t    calTotalPass = 0;              // bands passed across all phases
CalResult  calResult;

// Correction tables (RAM copy, persisted to EEPROM).
CalBand calTable[NUM_BANDS];
bool    calValid = false;    // a valid correction table is loaded

// ======================================================================
// FORWARD DECLARATIONS
// (A .ino auto-generates these; a .cpp must declare them before use.)
// ======================================================================

void handleStateMachine(bool startPressed, bool bandPressed, bool modePressed, bool calPressed);
void startScan();
void pollAnalyzer();
void processLine(char* line);
void updateDisplay();
void displayWelcome();
void drawWelcome();
void drawCurve(const Band& b);
void drawNumeric();
void startCalibrate();
void calNextPhase();
void calBeginBandSweep();
void calHandlePoint(const Measurement& m);
void calFinishBand();
void calFinishPhase();
void saveCalibration();
void loadCalibration();
void applyCalibration(Measurement& m);
void drawCalPrompt();
void drawCalProgress();
void drawCalDone();
void drawStatusOverlay();
bool isValidReading(float r, float x);
float computeSWR(float r, float x);
void emitState(const char* state);
void emitBand();
void emitMode();
void emitCalPhase();
void emitCalProgress();
void handlePcCommands(bool& s, bool& b, bool& m, bool& c);
bool isSweepStuck(uint32_t now);
void showStatus(const char* msg, uint32_t ms);

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

  // Try to abort a stuck scan/calibration sweep (silent analyzer) on a
  // timeout or any button press, so the unit stays responsive.
  if (isSweepStuck(now) || startPressed || bandPressed || modePressed || calPressed) {
    if (currentState == STATE_SCANNING || (currentState == STATE_CALIBRATE && calMeasuring)) {
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

// ======================================================================
// PC TELEMETRY + COMMANDS
// ======================================================================

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
        else if (strcmp(pcCmdBuf, "!GET:STATE") == 0) {
          emitState(currentState == STATE_WELCOME ? "WELCOME"
                  : currentState == STATE_IDLE ? "IDLE"
                  : currentState == STATE_CALIBRATE ? "CALIBRATE"
                  : currentState == STATE_CAL_DONE ? "CAL_DONE"
                  : currentState == STATE_SCANNING ? "SCANNING"
                  : "DISPLAYING");
          emitBand();
          emitMode();
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
// SCAN DRIVER
// ======================================================================

/**
 * @brief Command the AA-30 to sweep the currently selected band.
 *
 * Enters STATE_SCANNING, resets the point buffer, then powers the RF board
 * (`ON`) and issues `fq`/`sw`/`frx` for the selected band. The streamed
 * points are collected asynchronously by pollAnalyzer() -> processLine().
 */
void startScan() {
  scanCount   = 0;
  collecting  = true;
  sweepStart  = millis();
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
  // The analyzer needs a short gap between commands (esp. before frx) or it
  // drops the sweep request when the setup commands arrive back-to-back.
  AA_PORT.println("ON");
  delay(150);
  AA_PORT.print("fq"); AA_PORT.println(center);
  delay(50);
  AA_PORT.print("sw"); AA_PORT.println(span);
  delay(50);
  AA_PORT.print("frx"); AA_PORT.println(POINTS_PER_SCAN - 1);
}

// ======================================================================
// CALIBRATION / PERFORMANCE CHECK
// ======================================================================

/**
 * @brief Text describing what the user must connect for the current phase.
 *
 * @return Pointer to a static string for the current calPhase: a 50 ohm load,
 *         a short, or an open.
 */
const char* calPhasePrompt() {
  switch (calPhase) {
    case CAL_PHASE_50:    return "Connect 50 ohm load";
    case CAL_PHASE_SHORT: return "Connect SHORT";
    default:              return "Connect OPEN";
  }
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
  AA_PORT.println("ON");
  delay(50);
  AA_PORT.print("fq"); AA_PORT.println(center);
  AA_PORT.print("sw"); AA_PORT.println(span);
  AA_PORT.print("frx"); AA_PORT.println(CAL_PTS_PER_BAND - 1);
}

/**
 * @brief Store one measurement point from a calibration band sweep.
 *
 * For the 50 ohm phase, stores the additive correction (50 - R, 0 - X). For
 * the short/open phases, stores the raw R/X for verification. Advances the
 * per-band point counter and emits progress telemetry.
 *
 * @param m  The parsed, validated measurement point.
 */
void calHandlePoint(const Measurement& m) {
  if (!m.valid) return;

  CalBand& cb = calTable[calBandIndex];
  if (calPoint < CAL_PTS_PER_BAND) {
    cb.pts[calPoint].freqMHz = m.freqMHz;
    if (calPhase == CAL_PHASE_50) {
      // Reference is 50 ohm: store the additive correction.
      cb.pts[calPoint].rCorr = 50.0f - m.r;
      cb.pts[calPoint].xCorr = 0.0f - m.x;
    } else {
      cb.pts[calPoint].rCorr = m.r;   // raw reading for short/open verification
      cb.pts[calPoint].xCorr = m.x;
    }
    cb.count = calPoint + 1;
    calPoint++;
  }
  emitCalProgress();
}

/**
 * @brief Finalise the current band sweep and advance to the next.
 *
 * Marks the band valid (50 ohm phase), evaluates pass/fail for the short/open
 * phases, then either sweeps the next band or completes the phase when all
 * bands are done.
 */
void calFinishBand() {
  CalBand& cb = calTable[calBandIndex];

  if (calPhase == CAL_PHASE_50) {
    cb.valid = (cb.count == CAL_PTS_PER_BAND);
    if (cb.valid) {
      // rCorr = 50 - R_raw, so |rCorr| is the absolute R error.
      float rErr = 0.0f;
      for (uint8_t i = 0; i < cb.count; i++) {
        rErr += fabsf(cb.pts[i].rCorr);
      }
      rErr /= cb.count;
      if (rErr / 50.0f * 100.0f <= 10.0f) calTotalPass++;
    }
  } else {
    // SHORT: |R|,|X| both small.  OPEN: |R| or |X| large.
    bool ok = false;
    if (calPhase == CAL_PHASE_SHORT) {
      ok = true;
      for (uint8_t i = 0; i < cb.count; i++) {
        if (fabsf(cb.pts[i].rCorr) >= 10.0f || fabsf(cb.pts[i].xCorr) >= 10.0f) {
          ok = false;
          break;
        }
      }
    } else { // OPEN
      for (uint8_t i = 0; i < cb.count; i++) {
        if (fabsf(cb.pts[i].rCorr) > 500.0f || fabsf(cb.pts[i].xCorr) > 500.0f) {
          ok = true;
          break;
        }
      }
    }
    if (ok) calTotalPass++;
    else    calFailCount++;
  }

  calBandIndex++;
  if (calBandIndex < NUM_BANDS) {
    calBeginBandSweep();
  } else {
    calFinishPhase();
  }
}

/**
 * @brief Advance to the next calibration phase, or finish the wizard.
 *
 * After the 50 ohm phase, persists the correction table and moves to the
 * short-verification phase. After short, moves to open. After open, sets the
 * overall pass/fail result and enters STATE_CAL_DONE.
 */
void calFinishPhase() {
  calMeasuring = false;
  collecting   = false;
  calBandIndex = 0;
  calPoint     = 0;

  if (calPhase == CAL_PHASE_50) {
    // Persist the 50 ohm correction table now.
    saveCalibration();
    Serial.print("50 ohm phase: ");
    Serial.print(calTotalPass);
    Serial.print("/");
    Serial.print(NUM_BANDS);
    Serial.println(" bands within tolerance.");
    calPhase = CAL_PHASE_SHORT;
    calTotalPass = 0;
    emitCalPhase();
    Serial.println(calPhasePrompt());
    Serial.println("Press START to verify.");
  } else if (calPhase == CAL_PHASE_SHORT) {
    calPhase = CAL_PHASE_OPEN;
    calTotalPass = 0;
    emitCalPhase();
    Serial.println(calPhasePrompt());
    Serial.println("Press START to verify.");
  } else {
    // All phases done.
    calDone   = true;
    calPassed = (calFailCount == 0);
    currentState = STATE_CAL_DONE;
    emitState("CAL_DONE");
    Serial.print("CALIBRATION ");
    Serial.println(calPassed ? "PASSED" : "FAILED");
  }
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
 * Finds the band containing m.freqMHz, looks up the nearest stored correction
 * point, adds the R/X offsets, and recomputes SWR. No-op if no valid
 * calibration exists or the measurement is already invalid.
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
    // Nearest calibration point (simple lookup; points are ~evenly spaced).
    int best = 0;
    float bd = 1e9f;
    for (uint8_t i = 0; i < cb.count; i++) {
      float d = fabsf(cb.pts[i].freqMHz - m.freqMHz);
      if (d < bd) { bd = d; best = i; }
    }
    m.r += cb.pts[best].rCorr;
    m.x += cb.pts[best].xCorr;
    m.swr = computeSWR(m.r, m.x);
    m.valid = isValidReading(m.r, m.x) && m.swr >= 1.0f && m.swr <= MAX_SWR;
    return;
  }
}

// ======================================================================
// DATA PARSING & VALIDATION
// ======================================================================

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
 * @param line  Null-terminated line; modified in place (commas replaced by
 *              NUL terminators).
 * @param m     Output measurement (filled on success).
 * @return true if the line parsed and passed basic validation, else false.
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

  m.valid = isValidReading(m.r, m.x)
         && m.freqMHz >= 0.0f
         && m.freqMHz <= 200.0f       // analyzer range: 0.06 - 170 MHz
         && m.swr >= 1.0f
         && m.swr <= MAX_SWR;
  return m.valid;
}

/**
 * @brief Append a measurement to the scan buffer if space remains.
 *
 * @param m  Measurement to store.
 */
void storePoint(const Measurement& m) {
  if (scanCount < MAX_POINTS) {
    scanPoints[scanCount++] = m;
  }
}

// ======================================================================
// ANALYZER POLLING (line assembly + parse + print)
// ======================================================================

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
      if (calPoint >= CAL_PTS_PER_BAND) {
        calFinishBand();
      }
    } else {
      // Normal sweep: apply stored calibration correction, then store.
      applyCalibration(m);
      if (m.valid) {
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
    // Only the trailing frx OK (which arrives after points, scanCount > 0)
    // completes a normal scan; the fq/sw OKs arrive with scanCount == 0.
    // During calibration the wizard advances via calPoint, so ignore OK.
    if (collecting && scanCount > 0 && !calMeasuring) {
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
    // A data-shaped line that failed validation -> discard (bogus).
    Serial.print("<AA-30 BOGUS, discarded> ");
    Serial.println(s);
  }
}

// ======================================================================
// DISPLAY
// ======================================================================

/**
 * @brief Draw the boot welcome / button-instruction screen.
 *
 * Full-screen page (STATE_WELCOME): title bar plus a colour-coded list of the
 * four buttons and what each does, and a "press any button" hint.
 */
void displayWelcome() {
  tft.fillScreen(ILI9341_BLACK);

  // Title block.
  tft.fillRect(0, 0, 320, 44, ILI9341_BLUE);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
  tft.setTextSize(2);
  tft.setCursor(16, 8);
  tft.print("SWR METER");
  tft.setTextSize(1);
  tft.setCursor(16, 30);
  tft.print("Uno R4 + AA-30.ZERO");

  // Button instruction rows.
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(1);

  tft.setCursor(16, 58);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.print("[BAND]");
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(84, 58);
  tft.print("Select HF band (160m-10m)");

  tft.setCursor(16, 84);
  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  tft.print("[START]");
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(84, 84);
  tft.print("Scan current band");

  tft.setCursor(16, 110);
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.print("[MODE]");
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(84, 110);
  tft.print("Curve / numeric readout");

  tft.setCursor(16, 136);
  tft.setTextColor(ILI9341_ORANGE, ILI9341_BLACK);
  tft.print("[CAL]");
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(84, 136);
  tft.print("Calibration check");

  tft.setCursor(16, 172);
  tft.setTextColor(ILI9341_LIGHTGREY, ILI9341_BLACK);
  tft.print("Press any button to continue");

  Serial.println("==========================================================");
  Serial.println("SWR METER BOOTED: Uno R4 Minima + AA-30 Zero + ILI9341");
  Serial.println("[BAND] band  [START] scan  [MODE] layout  [CAL] calibrate");
  Serial.println("==========================================================");
}

/**
 * @brief Re-draw the welcome screen from the state machine.
 *
 * Kept separate from displayWelcome() so the boot-time call in setup() and the
 * STATE_WELCOME state-machine path share the same page.
 */
void drawWelcome() {
  displayWelcome();
}

/**
 * @brief Render the current page chrome and content.
 *
 * Draws the blue header (band + state), then delegates to the calibration
 * screens, the idle prompt, or the curve/numeric readout depending on the
 * current state and display mode.
 */
void updateDisplay() {
  const Band& b = BANDS[bandIndex];

  // Header line: band + state.
  tft.fillRect(0, 0, 320, 24, ILI9341_BLUE);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
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
    case STATE_DISPLAYING: tft.print("PRESS START"); break;
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
    tft.fillRect(0, 26, 320, 240 - 26, ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
    tft.setCursor(30, 120);
    tft.setTextSize(2);
    tft.print("Press [START]");
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
  tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
  tft.setCursor(80, 60);
  tft.print(statusMsg);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_LIGHTGREY, ILI9341_BLACK);
  tft.setCursor(60, 92);
  tft.print("Press any button...");
}

/**
 * @brief Draw the calibration prompt (which reference to connect).
 */
void drawCalPrompt() {
  tft.fillRect(0, 26, 320, 240 - 26, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(20, 40);
  tft.print("CALIBRATE");
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(20, 84);
  tft.print("Step ");
  tft.print((int)calPhase + 1);
  tft.print("/");
  tft.print(NUM_CAL_PHASES);
  tft.setCursor(20, 104);
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.print(calPhasePrompt());
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(20, 128);
  tft.print("Press START to sweep all bands");
  tft.setCursor(20, 150);
  tft.print("[MODE] cancel");
}

/**
 * @brief Draw the calibration progress (phase, band, point, progress bar).
 */
void drawCalProgress() {
  const Band& b = BANDS[calBandIndex];

  tft.fillRect(0, 26, 320, 240 - 26, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(20, 40);
  tft.print("Calibrating...");
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(20, 64);
  tft.print("Ref: ");
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.print(calPhasePrompt());
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
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
  int bw = 240;
  int bx = 20, by = 150;
  int fill = (int)((float)done / totalPts * bw);
  if (fill > bw) fill = bw;
  tft.drawRect(bx, by, bw, 12, ILI9341_WHITE);
  tft.fillRect(bx + 1, by + 1, fill - 1, 10, ILI9341_GREEN);
  tft.setCursor(20, 170);
  tft.print((done * 100) / totalPts);
  tft.print(" %  [MODE] cancel");
}

/**
 * @brief Draw the calibration wizard final summary (PASS/FAIL).
 */
void drawCalDone() {
  tft.fillRect(0, 0, 320, 240, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(calPassed ? ILI9341_GREEN : ILI9341_RED, ILI9341_BLACK);
  tft.setCursor(20, 40);
  tft.print(calPassed ? "CAL DONE" : "CAL FAILED");
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(20, 90);
  tft.print("Failures: ");
  tft.print((int)calFailCount);
  tft.setCursor(20, 110);
  tft.print(calValid ? "Correction saved" : "Correction NOT saved");
  tft.setCursor(20, 160);
  tft.print("Press any button to exit");
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
  const int x0 = 8, x1 = 312, y0 = 36, y1 = 224;
  const int plotW = x1 - x0;
  const int plotH = y1 - y0;

  tft.fillRect(x0 - 2, y0 - 8, plotW + 4, plotH + 16, ILI9341_BLACK);
  tft.drawRect(x0, y0, plotW, plotH, ILI9341_WHITE);

  // Y scaling: fixed 1.0 to 3.0 SWR window for readable comparison.
  const float swrMin = 1.0f, swrMax = 3.0f;

  // Draw SWR=2.0 reference gridline.
  int y2 = y1 - (int)((2.0f - swrMin) / (swrMax - swrMin) * plotH);
  tft.drawLine(x0, y2, x1, y2, ILI9341_DARKGREY);
  tft.setTextColor(ILI9341_DARKGREY, ILI9341_BLACK);
  tft.setCursor(x1 - 24, y2 - 10);
  tft.print("SWR2");

  uint16_t color = ILI9341_GREEN;
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
    color = (c.swr >= 2.0f) ? ILI9341_RED : (c.swr >= 1.5f ? ILI9341_YELLOW : ILI9341_GREEN);
    tft.drawLine(ax, ay, cx, cy, color);
  }

  // Footer inside plot: min SWR + frequency.
  float swrMinV = 1e9f; float fMin = 0.0f;
  for (uint16_t i = 0; i < scanCount; i++) {
    if (scanPoints[i].swr < swrMinV) { swrMinV = scanPoints[i].swr; fMin = scanPoints[i].freqMHz; }
  }
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(12, y1 + 6);
  tft.print("MinSWR ");
  tft.print(swrMinV, 2);
  tft.print(" @ ");
  tft.print(fMin, 3);
  tft.print(" MHz");
  tft.setCursor(140, y1 + 6);
  tft.print("n=");
  tft.print(scanCount);
}

/**
 * @brief Draw a large single-value numeric readout of the last measurement.
 */
void drawNumeric() {
  const Measurement& m = scanPoints[scanCount - 1];

  tft.fillRect(0, 26, 320, 240 - 26, ILI9341_BLACK);

  tft.setTextSize(3);
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.setCursor(10, 40);  tft.print("F ");
  tft.print(m.freqMHz, 3); tft.println(" MHz");

  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  tft.setCursor(10, 90);  tft.print("R  ");
  tft.print(m.r, 1);      tft.println(" ohm");

  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(10, 140); tft.print("X  ");
  tft.print(m.x, 1);      tft.println(" ohm");

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(10, 190); tft.print("SWR ");
  tft.print(m.swr, 2);
}
