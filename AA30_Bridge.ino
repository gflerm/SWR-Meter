// uart_bridge.ino
//
// UART Bridge for data exchange between
// RigExpert AA-30 ZERO antenna & cable analyzer and Arduino Uno R4 Minima
// Supports dual-channel communication (D1/D0 and D7/D4) and button control.
//
// 2024, opencode AI
//
#include <SoftwareSerial.h>

// ======================================================================
// HARDWARE DEFINITIONS
// ======================================================================

// GPIO Pins for Control Buttons (Using INPUT_PULLUP)
#define START_PIN 2 // Button Start Measurement
#define MODE_PIN  3 // Button Mode Select

// Define SoftwareSerial ports based on the specified pins
// Channel 1: D1 (RX) <--> D0 (TX)
SoftwareSerial ss1(D1, D0); 

// Channel 2: D7 (RX) <--> D4 (TX)
SoftwareSerial ss2(D7, D4); 

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

  // 2. Initialize Serial Bridges
  ss1.begin(38400);
  ss2.begin(38400);
  
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
  
  // General delay to manage loop rate
  delay(10);
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
      updateDisplay(startPressed, modeButtonPressed);
      break;

    case STATE_SCANNING:
      // Send commands and process streaming data
      handleSerialData();
      // Check if the AA-30 is finished scanning (requires parsing the output)
      // If finished: currentState = STATE_DISPLAYING;
      updateDisplay(startPressed, modeButtonPressed);
      
      break;

    case STATE_DISPLAYING:
      // Display the final processed data
      updateDisplay(startPressed, modeButtonPressed);
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
 * @brief Reads data from both serial bridges and routes the communication.
 */
void handleSerialData() {
  // 1. From AA-30 Zero to PC (Monitoring)
  if (ss1.available()) {
    char data = ss1.read();
    Serial.print("[Ch1] ");
    Serial.write(data);
  }

  // Check Channel 2
  if (ss2.available()) {
    char data = ss2.read();
    Serial.print("[Ch2] ");
    Serial.write(data);
  }

  // 2. From PC to AA-30 Zero (Commanding)
  if (Serial.available()) {
    char data = Serial.read();
    Serial.print("[TX/PC] ");
    Serial.write(data);

    // Send data to both channels for testing
    ss1.write(data); 
    ss2.write(data);
    Serial.println(" (Wrote to both channels)");
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
  
  // Send command over the primary channel (Channel 1)
  ss1.print(command); 
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