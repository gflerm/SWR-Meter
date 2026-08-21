// uart_bridge.ino
//
// UART Bridge for data exchange between
// RigExpert AA-30 ZERO antenna & cable analyzer and Arduino Uno R4 Minima
//
// NOTE (verified working): The AA-30.ZERO has two UARTs. On the Uno R4 Minima
// the ONLY reliable path is hardware Serial1 (D0 = RX, D1 = TX), which talks
// to the analyzer's UART1 interface. The R4's SoftwareSerial cannot drive the
// D4/D7 pins the analyzer's default UART2 uses (SoftwareSerial.begin() fails),
// so do NOT use SoftwareSerial with this board/analyzer combination.
//
//   AA-30.ZERO UART1 TX  ->  Uno D0  (Serial1 RX)
//   AA-30.ZERO UART1 RX  ->  Uno D1  (Serial1 TX)
//   AA-30.ZERO GND       ->  Uno GND (common ground required)
//
// 2024, opencode AI
//

// ======================================================================
// HARDWARE DEFINITIONS
// ======================================================================

// GPIO Pins for Control Buttons (Using INPUT_PULLUP)
#define START_PIN 2 // Button Start Measurement
#define MODE_PIN  3 // Button Mode Select

// AA-30 ZERO UART port (interface UART1 on hardware Serial1, D0/D1).
// Serial1: RX = D0, TX = D1.
#define AA_PORT Serial1

// System impedance used for SWR math (AA-30 uses 50 ohm by default).
#define Z0 50.0f

// Validation limits for rejecting bogus readings.
// A real antenna/load rarely exceeds these; bogus values (e.g. R = 50,455,205
// seen from a bad frx0 point) or NaN/Inf get discarded.
#define MAX_RESISTANCE 1000000.0f // ohms
#define MAX_REACTANCE  1000000.0f // ohms
#define MAX_SWR        100.0f     // physically impossible above this for HF

// A single validated measurement point from the AA-30 frx stream.
struct Measurement {
  float freqMHz; // frequency (MHz)
  float r;       // series resistance (ohm)
  float x;       // series reactance (ohm)
  float swr;     // computed standing wave ratio
  bool  valid;   // true only if it passed all checks
};

#define MAX_POINTS 256
Measurement scanPoints[MAX_POINTS];
uint16_t scanCount = 0;

// Placeholder for Display Library (e.g., <Wire.h>, <Adafruit_GFX.h>)
// #include <DisplayLibrary.h>
// Display display = Display();

// ======================================================================
// STATE MANAGEMENT
// ======================================================================

enum SystemState {
  STATE_IDLE,
  STATE_SELECT_BAND,
  STATE_SCANNING,
  STATE_DISPLAYING
};

SystemState currentState = STATE_IDLE;

void setup() {
  // 1. Initialize Pins
  pinMode(START_PIN, INPUT_PULLUP);
  pinMode(MODE_PIN, INPUT_PULLUP);

  // 2. Initialize AA-30 UART (hardware Serial1, UART1 interface @ 38400)
  AA_PORT.begin(38400);

  // 3. Initialize Debug/PC Serial
  Serial.begin(9600); 
  
  // 4. Initialize Display Components
  // display.begin();
  
  // Initial setup routine
  displayWelcome();
}

void loop() {
  // Read states of the buttons
  bool startButtonPressed = !digitalRead(START_PIN);
  bool modeButtonPressed = !digitalRead(MODE_PIN);

  // State Machine execution
  handleStateMachine(startButtonPressed, modeButtonPressed);

  // Always bridge AA-30 <--> PC serial data (enables test/console comms)
  handleSerialData();

  // General delay to manage loop rate
  delay(10);
}

// ======================================================================
// DATA PARSING & VALIDATION
// ======================================================================

/**
 * @brief Rejects physically impossible / bogus readings (NaN, Inf, or absurd
 *        magnitudes such as R = 50 million ohms).
 */
bool isValidReading(float r, float x) {
  if (!isfinite(r) || !isfinite(x)) return false;
  if (r <= 0.0f || r > MAX_RESISTANCE) return false;
  if (fabsf(x) > MAX_REACTANCE) return false;
  return true;
}

/**
 * @brief Computes SWR from series R and X at impedance Z0.
 * @return SWR, or a large sentinel value if the formula is degenerate.
 */
float computeSWR(float r, float x) {
  float num = sqrtf((r - Z0) * (r - Z0) + x * x);
  float den = sqrtf((r + Z0) * (r + Z0) + x * x);
  if (den <= 0.0f) return MAX_SWR + 1.0f; // degenerate -> reject
  float gamma = num / den;
  float d = 1.0f - gamma;
  if (d <= 0.0f) return MAX_SWR + 1.0f;   // reflection coefficient ~1 -> reject
  return (1.0f + gamma) / d;
}

/**
 * @brief Parses and validates an AA-30 data line: "freqMHz,R,X"
 * @return true and fills `m` only if the reading is physically plausible.
 */
bool parseFRXLine(const char* line, Measurement& m) {
  // 3 comma-separated fields: fq,R,X (fq in MHz).
  char buf[64];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char* p1 = strchr(buf, ',');
  if (!p1) return false;
  *p1 = '\0';
  char* p2 = strchr(p1 + 1, ',');
  if (!p2) return false;
  *p2 = '\0';

  m.freqMHz = atof(buf);
  m.r      = atof(p1 + 1);
  m.x      = atof(p2 + 1);
  m.swr    = computeSWR(m.r, m.x);

  m.valid = isValidReading(m.r, m.x)
         && m.freqMHz >= 0.0f
         && m.swr >= 1.0f          // SWR can never be below 1
         && m.swr <= MAX_SWR;

  return m.valid;
}

/**
 * @brief Stores a validated measurement point for later display/processing.
 */
void storePoint(const Measurement& m) {
  if (scanCount < MAX_POINTS) {
    scanPoints[scanCount++] = m;
  }
}

// ======================================================================
// STATE MACHINE LOGIC
// ======================================================================

void handleStateMachine(bool startPressed, bool modePressed) {
  switch (currentState) {
    case STATE_IDLE:
      // Wait for start button press to transition to band selection
      if (startPressed) {
        currentState = STATE_SELECT_BAND;
        Serial.println("Transitioning to: Band Selection Mode");
      }
      // Update Display only in IDLE state
      updateDisplay(startPressed, modePressed); 
      break;

    case STATE_SELECT_BAND:
      // Logic to check mode button press to cycle bands (e.g., 160m, 20m, 10m)
      // For now, just show selection state
      updateDisplay(startPressed, modePressed);
      break;

    case STATE_SCANNING:
      // Check if the AA-30 is finished scanning (requires parsing the output)
      // If finished: currentState = STATE_DISPLAYING;
      updateDisplay(startPressed, modePressed);
      
      break;

    case STATE_DISPLAYING:
      // Display the final processed data
      updateDisplay(startPressed, modePressed);
      // Wait for an external trigger or duration to loop back to IDLE
      // To loop: currentState = STATE_IDLE;
      break;
  }
}

// ======================================================================
// CORE FUNCTIONALITY
// ======================================================================

/**
 * @brief Displays the welcome screen and instructions when the board boots up.
 */
void displayWelcome() {
  // --- Placeholder for actual display library calls ---
  Serial.println("==========================================================");
  Serial.println("SWR METER BOOTED: Uno R4 Minima + AA-30 Zero");
  Serial.println("Instructions:");
  Serial.println("- Press [START] button to begin measurement.");
  Serial.println("- Press [MODE] button to select measuring band (160m/20m/10m).");
  Serial.println("==========================================================");
  // display.print("SWR Meter Operational. Press START.");
}

/**
 * @brief Bridges data between the AA-30 ZERO (Serial1) and the PC (Serial),
 *        buffering incoming lines, validating measurements, and computing SWR.
 */
void handleSerialData() {
  // Accumulate one line of AA-30 output at a time.
  static String aaLine;

  // 1. From AA-30 Zero to PC
  while (AA_PORT.available()) {
    char c = AA_PORT.read();

    if (c == '\n') {
      aaLine.trim();
      if (aaLine.length() > 0) {
        Measurement m;
        if (parseFRXLine(aaLine.c_str(), m)) {
          storePoint(m);
          // Summarise only valid points to the console.
          char buf[64];
          snprintf(buf, sizeof(buf),
                   "F=%.3fMHz R=%.1f X=%.1f SWR=%.2f",
                   m.freqMHz, m.r, m.x, m.swr);
          Serial.println(buf);
        } else if (aaLine.equalsIgnoreCase("OK")) {
          Serial.println("<AA-30 OK>");
        } else if (aaLine.indexOf(',') < 0) {
          // Non-data response (e.g. "AA-30.ZERO 200"): show as-is.
          Serial.println("<AA-30> " + aaLine);
        } else {
          // A data-shaped line that failed validation -> discard (bogus).
          Serial.println("<AA-30 BOGUS, discarded> " + aaLine);
        }
      }
      aaLine = "";
    } else {
      aaLine += c;
    }
  }

  // 2. From PC to AA-30 Zero
  while (Serial.available()) {
    AA_PORT.write(Serial.read());
  }
}

/**
 * @brief Sends structured commands to the AA-30 Zero to set the desired bandwidth/band.
 * @param band float The frequency (e.g., 160.0, 20.0, 10.0).
 * @param bandUnit String suffix ("M" or "K").
 */
void sendBandCommand(float band, const char* bandUnit) {
  // --- PLACEHOLDER ---
  String command = "SET_BAND:";
  command += String(band);
  command += bandUnit;
  command += "\n";
  
  Serial.print("Commanding AA-30 Zero to set band to: ");
  Serial.println(command);
  
  // Send command over the AA-30 UART (Serial1)
  AA_PORT.print(command); 
}

/**
 * @brief Parses raw serial data, extracts relevant values, and updates the SWR curve data structure.
 * @param dataChar The newly received raw character.
 */
void processReadings(char dataChar) {
  // --- PLACEHOLDER ---
  // This function is where the complex parsing logic goes.
  // It should look for specific delimiters/patterns in the serial stream.
  // Example: If data is "SWR=4.2,FREQ=140.0,RX=1.2", parse these fields.
  
  // Simulated data processing:
  // float swr = extractSWR();
  // float frequency = extractFrequency();
  // surfCurveData.addPoint(frequency, swr);
  
  Serial.println("[PARSER] Raw data received. Parsing...");
}

/**
 * @brief Updates the physical display with the calculated SWR curve, frequency, and status.
 */
void updateDisplay(bool startPressed, bool modePressed) {
  // --- PLACEHOLDER ---
  // This function will be implemented with the chosen display library.
  // It reads the latest values from the internal data structures (e.g., global SWR curve).
  
  // Example:
  // display.clearDisplay();
  // display.setTextSize(2);
  // display.setCursor(0, 0);
  // display.print("SWR:");
  // display.print(currentState == STATE_DISPLAYING ? 3.8 : 0.0);
  // display.println(" X:");
  // local_x_value = 100;
  // display.display();
}