// config.h
//
// Shared hardware pin map, build-time constants, and data types for the
// AA-30.ZERO SWR meter. Included by every translation unit; contains no state
// and no hardware objects, so it is safe to include from many .cpp files.
//
// 2024, opencode AI

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

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

// ---- Calibration wizard / correction table ---------------------------
// The wizard sweeps every HF band against reference loads, then stores a
// per-band per-point R/X offset table in EEPROM. Normal sweeps apply the
// nearest stored offset to correct the analyzer readings.
#define CAL_PTS_PER_BAND 20   // sweep points per band during calibration
#define CAL_EEPROM_MAGIC 0x4C4E31  // "CAL1" marker
#define CAL_EEPROM_ADDR  0

// Corrections applied after a band sweep, before the band is accepted.
#define CAL_MAX_RETRIES 3   // single-point re-measures per bogus slot
#define CAL_PASS_PCT   90   // minimum % of valid points to accept a band

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

// One correction entry: an additive offset to apply at a given frequency.
typedef struct {
  float freqMHz;  // measurement frequency (MHz)
  float rCorr;    // add to measured R to get corrected R
  float xCorr;    // add to measured X to get corrected X
  bool  valid;    // this point produced a physically plausible reading
} CalPoint;

// Correction table for one band (CAL_PTS_PER_BAND entries).
typedef struct {
  bool     valid;
  uint8_t  count;
  CalPoint pts[CAL_PTS_PER_BAND];
} CalBand;

// Calibration wizard phases (in order). Only the 50 ohm phase matters: it
// builds and stores the correction table. SHORT/OPEN were verification-only
// (no correction, no EEPROM write), so they were removed to keep the wizard
// to a single plug / pass.
enum CalPhase {
  CAL_PHASE_50     // sweep 50 ohm -> build offsets
};
#define NUM_CAL_PHASES 1

#endif // CONFIG_H
