// hardware.cpp
//
// Definitions of every shared global declared in hardware.h, plus the two
// hardware objects. The AA-30 UART is initialised here so the stream objects
// exist before the first frame of the display in setup().
//
// 2024, opencode AI

#include "hardware.h"

// ---- hardware objects ------------------------------------------------

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ---- machine-wide state ----------------------------------------------

SystemState  currentState = STATE_WELCOME;
DisplayMode  displayMode  = MODE_CURVE;
uint8_t      bandIndex    = 0;

bool         extControl   = false;
bool         extSplashDone = false;

Measurement  scanPoints[MAX_POINTS];
uint16_t     scanCount     = 0;

char         lineBuf[LINE_BUF];
uint8_t      lineLen       = 0;
bool         collecting    = false;

uint32_t     sweepStart    = 0;

const char*  statusMsg     = NULL;
uint32_t     statusMsgUntil = 0;

char         pcCmdBuf[LINE_BUF];
uint8_t      pcCmdLen      = 0;

// ---- calibration / performance-check state ---------------------------

uint8_t    calPhase       = CAL_PHASE_50;
uint8_t    calBandIndex   = 0;
uint8_t    calPoint       = 0;
bool       calActive      = false;
bool       calMeasuring   = false;
bool       calFinishPending = false;
bool       calVerifying   = false;
float      calRetryFreq   = 0.0f;
bool       calRetrying    = false;
uint8_t    calRetryIdx    = 0;
uint8_t    calRetryUsed[CAL_PTS_PER_BAND];
bool       calDone        = false;
bool       calPassed      = false;
uint8_t    calFailCount   = 0;
uint8_t    calTotalPass   = 0;
CalResult  calResult;

CalBand calTable[NUM_BANDS];
bool    calValid = false;
