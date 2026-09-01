// battery.cpp
//
// 2024, opencode AI

#include "battery.h"
#include "config.h"
#include "hardware.h"

#define MAX17043_ADDR   0x36
#define MAX17043_VCELL  0x02
#define MAX17043_SOC    0x04
#define MAX17043_CONFIG 0x0C
#define MAX17043_COMMAND 0xFE

// I2C helpers (share the Wire bus used by the GT911 touch).
static void batWrite16(uint8_t reg, uint16_t dat) {
  Wire.beginTransmission(MAX17043_ADDR);
  Wire.write(reg);
  Wire.write(dat >> 8);
  Wire.write(dat & 0xFF);
  Wire.endTransmission();
}

static uint16_t batRead16(uint8_t reg) {
  Wire.beginTransmission(MAX17043_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom((int)MAX17043_ADDR, 2);
  uint16_t v = (uint16_t)Wire.read() << 8;
  v |= (uint16_t)Wire.read();
  return v;
}

// Read-modify-write a bit field in CONFIG.
static void batWriteRegBits(uint8_t reg, uint16_t dat, uint16_t bits, uint8_t offset) {
  uint16_t t = batRead16(reg);
  t = (t & (~(bits << offset))) | (dat << offset);
  batWrite16(reg, t);
}

int batteryBegin() {
  // Probe the I2C address: if nothing ACKs, the shield/gauge isn't on the bus.
  Wire.beginTransmission(MAX17043_ADDR);
  if (Wire.endTransmission() != 0) {
    return -1;   // no device at 0x36
  }
  // Power-on reset.
  batWrite16(MAX17043_COMMAND, 0x5400);
  delay(10);
  // Quick-start for a faster, more accurate SOC estimate.
  batWrite16(0x06, 0x4000);   // MAX17043_MODE quick-start
  delay(10);
  return 0;
}

float batteryVoltageMv() {
  uint16_t vcell = batRead16(MAX17043_VCELL);
  return 1.25f * (float)(vcell >> 4);
}

float batteryPercent() {
  uint16_t per = batRead16(MAX17043_SOC);
  return (float)((per >> 8) + 0.003906f * (per & 0x00FF));
}

bool batterySetAlert(uint8_t per) {
  if (per > 32) per = 32;
  if (per < 1)  per = 1;
  uint16_t temp = 32 - per;
  batWriteRegBits(MAX17043_CONFIG, temp, 0x1F, 0);
  // Read back the alert flag (bit 5) to report current low-battery state.
  return (batRead16(MAX17043_CONFIG) & 0x20) != 0;
}

void batteryClearAlert() {
  batWriteRegBits(MAX17043_CONFIG, 0, 0x01, 5);
}

bool batteryLowAlertActive() {
  // The shield breaks the ALRT pin out to D2 (active-low).
  return digitalRead(BAT_ALERT_PIN) == LOW;
}
