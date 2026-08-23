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

// ======================================================================
// INCLUDES
// ======================================================================
#include <Arduino.h>
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

// Calibration / performance-check state.
uint8_t    calRefIndex = 0;
bool       calActive = false;
bool       calMeasuring = false;
CalResult  calResult;

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
void runCalMeasurement();
void evaluateCalibration(const Measurement& m, CalResult& res);
void printCalResult();
void drawCalPrompt();
void drawCalResult();

// ======================================================================
// SETUP / LOOP
// ======================================================================

void setup() {
  pinMode(START_PIN, INPUT_PULLUP);
  pinMode(BAND_PIN,  INPUT_PULLUP);
  pinMode(MODE_PIN,  INPUT_PULLUP);
  pinMode(CAL_PIN,   INPUT_PULLUP);

  AA_PORT.begin(38400);   // AA-30 analyzer UART1
  Serial.begin(PC_BAUD);  // PC console / telemetry

  tft.begin();
  tft.setRotation(1);            // landscape (320x240)
  tft.fillScreen(ILI9341_BLACK);

  displayWelcome();
}

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

  handleStateMachine(startPressed, bandPressed, modePressed, calPressed);

  // Always absorb any analyzer bytes into the line buffer; in scan mode the
  // completion of the stream (a trailing "OK") drives the state transition.
  pollAnalyzer();

  // In IDLE we also pass through PC -> analyzer (lets external tools drive it).
  if (currentState == STATE_IDLE) {
    while (Serial.available()) {
      AA_PORT.write(Serial.read());
    }
  }

  delay(2);
}

// ======================================================================
// STATE MACHINE
// ======================================================================

void handleStateMachine(bool startPressed, bool bandPressed, bool modePressed, bool calPressed) {
  switch (currentState) {
    case STATE_WELCOME:
      // Boot-up instructions page: any button advances to IDLE.
      drawWelcome();
      if (startPressed || bandPressed || modePressed || calPressed) {
        currentState = STATE_IDLE;
        updateDisplay();
      }
      break;

    case STATE_IDLE:
      // BAND cycles the selected HF band.
      if (bandPressed) {
        bandIndex = (bandIndex + 1) % NUM_BANDS;
        Serial.print("Band selected: ");
        Serial.println(BANDS[bandIndex].name);
      }
      // MODE toggles the display layout.
      if (modePressed) {
        displayMode = (displayMode == MODE_CURVE) ? MODE_NUMERIC : MODE_CURVE;
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
      // BAND cycles the reference load; MODE cancels back to IDLE.
      if (bandPressed) {
        calRefIndex = (calRefIndex + 1) % NUM_CAL_REFS;
        Serial.print("Cal reference: ");
        Serial.println(CAL_REFS[calRefIndex].name);
      }
      if (modePressed) {
        calActive = false;
        currentState = STATE_IDLE;
      }
      // START takes a single-frequency reference measurement.
      if (startPressed && !calMeasuring) {
        runCalMeasurement();
      }
      updateDisplay();
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
      // During a calibration result, BAND picks a new reference and
      // returns to the prompt; otherwise START returns to IDLE.
      if (calActive) {
        if (bandPressed) {
          calRefIndex = (calRefIndex + 1) % NUM_CAL_REFS;
          currentState = STATE_CALIBRATE;
          Serial.print("Cal reference: ");
          Serial.println(CAL_REFS[calRefIndex].name);
        }
        if (startPressed) {
          calActive = false;
          currentState = STATE_IDLE;
        }
      } else if (startPressed) {
        currentState = STATE_IDLE;
      }
      break;
  }
}

// ======================================================================
// SCAN DRIVER
// ======================================================================

void startScan() {
  scanCount   = 0;
  collecting  = true;
  currentState = STATE_SCANNING;

  const Band& b = BANDS[bandIndex];
  uint32_t center = (b.low + b.high) / 2;
  uint32_t span   = b.high - b.low;

  Serial.print("Scanning ");
  Serial.print(b.name);
  Serial.print("  center=");
  Serial.print(center);
  Serial.print(" Hz  span=");
  Serial.print(span);
  Serial.println(" Hz");

  AA_PORT.print("fq"); AA_PORT.println(center);
  AA_PORT.print("sw"); AA_PORT.println(span);
  AA_PORT.print("frx"); AA_PORT.println(POINTS_PER_SCAN - 1);
}

// ======================================================================
// CALIBRATION / PERFORMANCE CHECK
// ======================================================================

// Enters the calibration screen (does not measure yet; waits for [START]).
void startCalibrate() {
  calActive    = true;
  calMeasuring = false;
  scanCount    = 0;
  currentState = STATE_CALIBRATE;
  Serial.println("CALIBRATE: connect a reference load and press START.");
}

// Takes a single-frequency measurement at the current band centre against
// the selected reference (fq + sw0 + frx0). Results arrive in pollAnalyzer().
void runCalMeasurement() {
  const Band& b = BANDS[bandIndex];
  uint32_t center = (b.low + b.high) / 2;

  scanCount   = 0;
  collecting  = true;
  calMeasuring = true;
  currentState = STATE_CALIBRATE;   // remains until a valid point arrives

  Serial.print("Cal measure @ ");
  Serial.print(center);
  Serial.print(" Hz, ref=");
  Serial.println(CAL_REFS[calRefIndex].name);

  AA_PORT.print("fq"); AA_PORT.println(center);
  AA_PORT.println("sw0");
  AA_PORT.println("frx0");
}

// Compares a measurement against the selected reference and fills calResult.
void evaluateCalibration(const Measurement& m, CalResult& res) {
  const CalRef& ref = CAL_REFS[calRefIndex];

  res.valid   = m.valid;
  res.r       = m.r;
  res.x       = m.x;
  res.swr     = m.swr;
  res.refName = ref.name;
  res.pass    = false;
  res.devPct  = 0.0f;

  if (!m.valid) return;

  if (ref.isOpen) {
    // Open reference: expect a high impedance (large |R| or large |X|).
    res.pass = (fabsf(m.r) > 500.0f) || (fabsf(m.x) > 500.0f);
  } else if (ref.isShort) {
    // Short reference: expect near-zero impedance.
    res.pass = (fabsf(m.r) < 10.0f) && (fabsf(m.x) < 10.0f);
  } else {
    res.devPct = fabsf(m.r - ref.rExp) / ref.rExp * 100.0f;
    res.pass   = (res.devPct <= ref.tolPct);
  }
}

// Prints the calibration result to the PC console.
void printCalResult() {
  const CalRef& ref = CAL_REFS[calRefIndex];

  Serial.println("=== CALIBRATION CHECK ===");
  Serial.print("Reference: "); Serial.println(ref.name);
  Serial.print("R="); Serial.print(calResult.r, 1);
  Serial.print("  X="); Serial.print(calResult.x, 1);
  Serial.print("  SWR="); Serial.println(calResult.swr, 2);
  if (!calResult.valid) {
    Serial.println("RESULT: INVALID (no/bogus reading)");
    return;
  }
  if (ref.isOpen || ref.isShort) {
    Serial.print("RESULT: "); Serial.println(calResult.pass ? "PASS" : "FAIL");
  } else {
    Serial.print("Deviation: "); Serial.print(calResult.devPct, 1);
    Serial.print(" %  RESULT: "); Serial.println(calResult.pass ? "PASS" : "FAIL");
  }
}

// ======================================================================
// DATA PARSING & VALIDATION
// ======================================================================

bool isValidReading(float r, float x) {
  if (!isfinite(r) || !isfinite(x)) return false;
  if (r <= 0.0f || r > MAX_RESISTANCE) return false;
  if (fabsf(x) > MAX_REACTANCE) return false;
  return true;
}

float computeSWR(float r, float x) {
  float num = sqrtf((r - Z0) * (r - Z0) + x * x);
  float den = sqrtf((r + Z0) * (r + Z0) + x * x);
  if (den <= 0.0f) return MAX_SWR + 1.0f; // degenerate -> reject
  float gamma = num / den;
  float d = 1.0f - gamma;
  if (d <= 0.0f) return MAX_SWR + 1.0f;   // |gamma| ~ 1 -> reject
  return (1.0f + gamma) / d;
}

// Parses "freqMHz,R,X" in place. Returns true and fills m if plausible.
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

void storePoint(const Measurement& m) {
  if (scanCount < MAX_POINTS) {
    scanPoints[scanCount++] = m;
  }
}

// ======================================================================
// ANALYZER POLLING (line assembly + parse + print)
// ======================================================================

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
    storePoint(m);
    if (calMeasuring) {
      // Single-point calibration reference measurement (frx0 -> one line).
      evaluateCalibration(m, calResult);
      calMeasuring = false;
      collecting  = false;
      currentState = STATE_DISPLAYING;
      printCalResult();
    } else if (collecting && scanCount >= POINTS_PER_SCAN) {
      collecting = false;
      currentState = STATE_DISPLAYING;
    }
    // Telemetry to PC (6 decimals for MHz preserves narrow-band resolution).
    char buf[64];
    snprintf(buf, sizeof(buf), "F=%.6fMHz R=%.1f X=%.1f SWR=%.2f",
             m.freqMHz, m.r, m.x, m.swr);
    Serial.println(buf);
  } else if (strcasecmp(s, "OK") == 0) {
    Serial.println("<AA-30 OK>");
    // Only the trailing frx OK (which arrives after points, scanCount > 0)
    // completes a scan; the fq/sw OKs arrive with scanCount == 0.
    if (collecting && scanCount > 0) {
      collecting = false;
      currentState = STATE_DISPLAYING;
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

// Full-screen welcome + button instructions shown on boot (STATE_WELCOME).
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

// Re-draws the welcome screen from the state machine (kept separate so the
// boot-time call in setup() and the state-machine path share the same page).
void drawWelcome() {
  displayWelcome();
}

// Draws the page chrome + either a curve or numeric readout from scanPoints[].
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
    case STATE_CALIBRATE:  tft.print("CALIBRATE"); break;
    case STATE_SCANNING:   tft.print("SCANNING..."); break;
    case STATE_DISPLAYING: tft.print(calActive ? "CAL RESULT" : "PRESS START"); break;
  }

  // Calibration screens take over the page body.
  if (currentState == STATE_CALIBRATE) {
    drawCalPrompt();
    return;
  }
  if (calActive && currentState == STATE_DISPLAYING) {
    drawCalResult();
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
    return;
  }

  if (displayMode == MODE_CURVE) {
    drawCurve(b);
  } else {
    drawNumeric();
  }
}

// Calibration prompt: tells the user which reference to connect and how.
void drawCalPrompt() {
  const CalRef& ref = CAL_REFS[calRefIndex];

  tft.fillRect(0, 26, 320, 240 - 26, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(20, 40);
  tft.print("CALIBRATE");
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(20, 80);
  tft.print("Reference: ");
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.print(ref.name);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(20, 110);
  tft.print("Connect load, press START");
  tft.setCursor(20, 140);
  tft.print("[BAND] change  [START] run");
  tft.setCursor(20, 160);
  tft.print("[MODE] cancel");
}

// Calibration result: PASS/FAIL, R/X/SWR and deviation from the reference.
void drawCalResult() {
  const CalRef& ref = CAL_REFS[calRefIndex];

  tft.fillRect(0, 26, 320, 240 - 26, ILI9341_BLACK);

  tft.setTextSize(2);
  if (!calResult.valid) {
    tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft.setCursor(20, 40);
    tft.print("NO READING");
  } else {
    tft.setTextColor(calResult.pass ? ILI9341_GREEN : ILI9341_RED, ILI9341_BLACK);
    tft.setCursor(20, 40);
    tft.print(calResult.pass ? "PASS" : "FAIL");
  }

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(20, 80);
  tft.print("Ref: "); tft.print(ref.name);
  tft.setCursor(20, 104);
  tft.print("R="); tft.print(calResult.r, 1);
  tft.print("  X="); tft.print(calResult.x, 1);
  tft.setCursor(20, 124);
  tft.print("SWR="); tft.print(calResult.swr, 2);
  if (!(ref.isOpen || ref.isShort)) {
    tft.setCursor(20, 144);
    tft.print("Dev "); tft.print(calResult.devPct, 1); tft.print(" %");
  }
  tft.setCursor(20, 176);
  tft.print("[START] exit  [BAND] new");
}

// Scales scanPoints[] into the plot area and draws the SWR curve.
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

// Big single-value numeric readout (last valid measurement).
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
