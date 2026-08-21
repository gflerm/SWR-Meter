// uart_bridge.ino
//
// UART Bridge between a RigExpert AA-30.ZERO antenna & cable analyzer and an
// Arduino Uno R4 Minima. The AA-30.ZERO has two UARTs; on the R4 the only
// reliable path is hardware Serial1 (D0 = RX, D1 = TX), which talks to the
// analyzer's UART1 interface.
//
// The R4's SoftwareSerial cannot drive the D4/D7 pins the analyzer's default
// UART2 uses (SoftwareSerial.begin() fails), so UART1 + Serial1 is required.
//
// Wiring:
//   AA-30.ZERO UART1 TX  ->  Uno D0  (Serial1 RX)
//   AA-30.ZERO UART1 RX  ->  Uno D1  (Serial1 TX)
//   AA-30.ZERO GND       ->  Uno GND (common ground required)
//   Analyzer must be set to UART1 (default is UART2 on the board).
//
// 2024, opencode AI

// ======================================================================
// HARDWARE DEFINITIONS
// ======================================================================

// Control buttons (INPUT_PULLUP)
#define START_PIN 2   // Start measurement
#define MODE_PIN  3   // Select band

// AA-30 ZERO UART port (interface UART1, hardware Serial1: RX = D0, TX = D1)
#define AA_PORT Serial1

// PC console / telemetry port. 115200 keeps the AA-30 -> PC direction from
// being the bottleneck while streaming many measurement points.
#define PC_BAUD 115200

// System impedance used for SWR math (AA-30 measures in a 50 ohm system by default)
#define Z0 50.0f

// Validation limits for rejecting bogus readings (NaN/Inf or absurd magnitudes).
#define MAX_RESISTANCE 1000000.0f  // ohms
#define MAX_REACTANCE  1000000.0f  // ohms
#define MAX_SWR        100.0f      // physically impossible above this for HF

// Fitted line buffer for AA-30 ASCII output (one CSV line at a time).
#define LINE_BUF 96

// Device-side max points retained for display; reset on every scan.
#define MAX_POINTS 256
#define POINTS_PER_SCAN 100   // points requested per band (fits in MAX_POINTS)

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

enum SystemState {
  STATE_IDLE,
  STATE_SCANNING,
  STATE_DISPLAYING
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

// ======================================================================
// GLOBALS
// ======================================================================

SystemState currentState = STATE_IDLE;
uint8_t     bandIndex     = 0;

Measurement scanPoints[MAX_POINTS];
uint16_t    scanCount     = 0;

// AA-30 line assembler (fixed buffer, no String / heap use).
char      lineBuf[LINE_BUF];
uint8_t   lineLen = 0;
bool      collecting = false;  // true while awaiting frx data

// ======================================================================
// SETUP / LOOP
// ======================================================================

void setup() {
  pinMode(START_PIN, INPUT_PULLUP);
  pinMode(MODE_PIN, INPUT_PULLUP);

  AA_PORT.begin(38400);   // AA-30 analyzer UART1
  Serial.begin(PC_BAUD);  // PC console / telemetry

  displayWelcome();
}

void loop() {
  bool startPressed = !digitalRead(START_PIN);
  bool modePressed  = !digitalRead(MODE_PIN);

  handleStateMachine(startPressed, modePressed);

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

void handleStateMachine(bool startPressed, bool modePressed) {
  switch (currentState) {
    case STATE_IDLE:
      // MODE cycles the selected band.
      if (modePressed) {
        bandIndex = (bandIndex + 1) % NUM_BANDS;
        Serial.print("Band selected: ");
        Serial.println(BANDS[bandIndex].name);
      }
      // START runs a scan of the current band.
      if (startPressed) {
        startScan();
      }
      updateDisplay();
      break;

    case STATE_SCANNING:
      // pollAnalyzer() turns the trailing "OK" into STATE_DISPLAYING.
      updateDisplay();
      break;

    case STATE_DISPLAYING:
      updateDisplay();
      // START returns to IDLE (press again to run a fresh scan).
      if (startPressed) {
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
    if (collecting && scanCount >= POINTS_PER_SCAN) {
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
// DISPLAY (placeholder until a display is selected)
// ======================================================================

void updateDisplay() {
  // TODO: render the SWR curve from scanPoints[] onto the chosen display.
}

void displayWelcome() {
  Serial.println("==========================================================");
  Serial.println("SWR METER BOOTED: Uno R4 Minima + AA-30 Zero");
  Serial.print("[MODE] select band, [START] scan. PC console @ ");
  Serial.print(PC_BAUD);
  Serial.println(" baud.");
  Serial.println("==========================================================");
}
