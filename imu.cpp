#include <Arduino.h>
#include <Wire.h>
#include "imu.h"

// Waveshare ESP32-S3-Touch-LCD-1.28: QMI8658 IMU + CST816 touch share I2C on SDA=6 / SCL=7.
// Register map is sequential: CTRL1=0x02, CTRL2=0x03, CTRL3=0x04 ... CTRL7=0x08 (the numbering
// does NOT match the address -- CTRL2 accel config lives at 0x03, not 0x06).
#define IMU_SDA 6
#define IMU_SCL 7

bool imuPresent = false;
static uint8_t imuAddr = 0x6B;   // QMI8658 SA0=high default; 0x6A if strapped low

static bool wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(imuAddr); Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}
static int rd(uint8_t reg) {
  Wire.beginTransmission(imuAddr); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)imuAddr, 1) != 1) return -1;
  return Wire.read();
}

bool imuBegin() {
#if !defined(BOARD_WAVESHARE_128)
  return false;   // IMU only on the Waveshare board; elsewhere SDA/SCL 6/7 are the display CS/RST -- don't claim them
#else
  Wire.begin(IMU_SDA, IMU_SCL);
  Wire.setClock(400000);
  for (uint8_t a : {0x6B, 0x6A}) {          // probe both possible addresses
    imuAddr = a;
    if (rd(0x00) == 0x05) { imuPresent = true; break; }   // WHO_AM_I == 0x05
  }
  if (!imuPresent) return false;
  wr(0x60, 0xB0);   // soft reset -- an ESP32 EN-reset doesn't power-cycle the IMU, so start from a known state
  for (int i = 0; i < 40 && rd(0x4D) != 0x80; i++) delay(5);   // wait for reset-done before configuring
  wr(0x02, 0x40);   // CTRL1: address auto-increment, little-endian, internal osc on
  wr(0x03, 0x16);   // CTRL2 (accel, reg 0x03!): +/-4g, aODR=6 (125Hz)
  wr(0x04, 0x53);   // CTRL3 (gyro): +/-512dps, gODR ~=112Hz -- validated on the Waveshare board
  wr(0x08, 0x03);   // CTRL7: enable accel (bit0) + gyro (bit1)
  delay(20);
  int16_t d; for (int i = 0; i < 3; i++) { imuReadAccel(&d, &d, &d); delay(10); }  // flush invalid startup samples
  return true;
#endif
}

bool imuReadAccel(int16_t* ax, int16_t* ay, int16_t* az) {
  if (!imuPresent) return false;
  Wire.beginTransmission(imuAddr); Wire.write(0x35);       // AccX_L, auto-increments through AccZ_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)imuAddr, 6) != 6) return false;
  uint8_t b[6]; for (int i = 0; i < 6; i++) b[i] = Wire.read();
  *ax = (int16_t)(b[0] | (b[1] << 8));
  *ay = (int16_t)(b[2] | (b[3] << 8));
  *az = (int16_t)(b[4] | (b[5] << 8));
  return true;
}

bool imuReadGyro(int16_t* gx, int16_t* gy, int16_t* gz) {
  if (!imuPresent) return false;
  Wire.beginTransmission(imuAddr); Wire.write(0x3B);       // GyrX_L, auto-increments through GyrZ_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)imuAddr, 6) != 6) return false;
  uint8_t b[6]; for (int i = 0; i < 6; i++) b[i] = Wire.read();
  *gx = (int16_t)(b[0] | (b[1] << 8));
  *gy = (int16_t)(b[2] | (b[3] << 8));
  *gz = (int16_t)(b[4] | (b[5] << 8));
  return true;
}

uint8_t imuRotation() {
  // Screen-vertical axis on this board is accel X (measured): upright ~ +1g, inverted ~ -1g.
  // Dead zone: when the board is flat/tilted-away the in-plane pull is weak -> hold the last decision
  // instead of flickering. Below ~0.5g we can't tell up from down, so keep `rot`.
  static uint8_t rot = 0;
  int16_t ax, ay, az;
  if (!imuReadAccel(&ax, &ay, &az)) return rot;
  const int16_t TH = 4000;                 // ~0.5g of the 8192 counts/g at +/-4g
  if (ax >  TH) rot = 2;                    // accel +X = board held one way -> 180 so the eye reads upright
  else if (ax < -TH) rot = 0;              // accel -X = the other way -> no flip
  return rot;
}
