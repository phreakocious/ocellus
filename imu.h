#pragma once
#include <stdint.h>

// QMI8658 6-axis IMU on the Waveshare ESP32-S3-Touch-LCD-1.28 (shared I2C bus SDA=6/SCL=7).
// Accelerometer drives auto-flip + Fluid gravity; gyro-Z supplies Fluid's spin impulse.

extern bool imuPresent;                 // false if the chip didn't answer (auto-flip then no-ops)

bool imuBegin();                        // true if QMI8658 found + accel/gyro configured
bool imuReadAccel(int16_t* ax, int16_t* ay, int16_t* az);   // raw signed counts, little-endian
bool imuReadGyro(int16_t* gx, int16_t* gy, int16_t* gz);   // raw signed counts (deg/s scale), little-endian
void imuGyroEnable(bool on);            // gyro draws ~1mA; onAnimEnter turns it on only for modes that read it
uint8_t imuRotation();                  // desired display rotation (0 or 2) from gravity, with a dead zone
