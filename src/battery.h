// battery.h
//
// DFRobot LiPower Shield MAX17043 LiPo fuel gauge over I2C (Wire, addr 0x36).
//
// The shield boosts a 3.7 V LiPo to 5 V to power the R4, and its MAX17043G+U
// fuel gauge reports battery voltage and state-of-charge. It shares the same
// I2C bus (A4/A5) as nothing else now (the touch is SPI/XPT2046), so the bus
// is dedicated to the gauge.
//
// 2024, opencode AI

#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

// Initialise the MAX17043 (power-on reset + quick-start). Returns 0 on success.
int  batteryBegin();

// Read battery voltage in millivolts (VCELL * 1.25 mV/LSB).
float batteryVoltageMv();

// Read battery remaining capacity (state of charge) in percent (0-100).
float batteryPercent();

// Set the low-battery alert threshold (1-32%). Below it the ALRT pin (D2)
// asserts. Returns true if the alert is currently asserted.
bool  batterySetAlert(uint8_t per);

// Clear a latched low-battery alert.
void  batteryClearAlert();

// True if the physical ALRT pin (D2) is low / the guard is asserted.
bool  batteryLowAlertActive();

#endif // BATTERY_H
