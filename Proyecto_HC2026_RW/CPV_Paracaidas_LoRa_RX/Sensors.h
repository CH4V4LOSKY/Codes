#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "FlightLogic.h"

inline bool writeReg(uint8_t addr, uint8_t reg, uint8_t value)
{
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}
inline bool readRegs(uint8_t addr, uint8_t reg, uint8_t *out, uint8_t n)
{
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0)
    return false;
  if (Wire.requestFrom(addr, n) != n)
    return false;
  for (uint8_t i = 0; i < n; i++)
    out[i] = uint8_t(Wire.read());
  return true;
}
inline int16_t signed16(const uint8_t *p) { return int16_t((uint16_t(p[0]) << 8) | p[1]); }
struct ImuSample
{
  Vec3 force, gyro;
  bool accelClipped = false, gyroClipped = false;
};
inline bool beginImu()
{
  uint8_t id = 0;
  if (!readRegs(0x68, 0x75, &id, 1) || (id & 0x7e) != 0x68)
    return false;
  if (!writeReg(0x68, 0x6b, 0x01))
    return false;
  vTaskDelay(pdMS_TO_TICKS(100)); // Other ESP32 core continues listening to radio.
  return writeReg(0x68, 0x1a, 0x03) && writeReg(0x68, 0x19, 9) &&
         writeReg(0x68, 0x1b, 0x18) && writeReg(0x68, 0x1c, 0x18) &&
         writeReg(0x68, 0x6a, 0) && writeReg(0x68, 0x37, 2);
}
inline bool readImu(ImuSample &sample)
{
  uint8_t raw[14];
  if (!readRegs(0x68, 0x3b, raw, 14))
    return false;
  const int16_t ax = signed16(raw), ay = signed16(raw + 2), az = signed16(raw + 4);
  const int16_t gx = signed16(raw + 8), gy = signed16(raw + 10), gz = signed16(raw + 12);
  // Registers configured for +/-16 g (2048 counts/g), +/-2000 deg/s (16.4).
  sample.force = Vec3{double(ax), double(ay), double(az)} * (9.80665 / 2048.0);
  sample.gyro = Vec3{double(gx), double(gy), double(gz)} * (3.141592653589793 / (180 * 16.4));
  sample.accelClipped = abs(int(ax)) >= 32700 || abs(int(ay)) >= 32700 || abs(int(az)) >= 32700;
  sample.gyroClipped = abs(int(gx)) >= 32700 || abs(int(gy)) >= 32700 || abs(int(gz)) >= 32700;
  return true;
}

// Original register-level BMP180 driver with checked I2C and wide arithmetic.
struct Bmp180
{
  int16_t ac1 = 0, ac2 = 0, ac3 = 0, b1 = 0, b2 = 0, mb = 0, mc = 0, md = 0;
  uint16_t ac4 = 0, ac5 = 0, ac6 = 0;
  bool begin()
  {
    uint8_t id, raw[22];
    if (!readRegs(0x77, 0xd0, &id, 1) || id != 0x55 || !readRegs(0x77, 0xaa, raw, 22))
      return false;
    ac1 = signed16(raw);
    ac2 = signed16(raw + 2);
    ac3 = signed16(raw + 4);
    ac4 = uint16_t(signed16(raw + 6));
    ac5 = uint16_t(signed16(raw + 8));
    ac6 = uint16_t(signed16(raw + 10));
    b1 = signed16(raw + 12);
    b2 = signed16(raw + 14);
    mb = signed16(raw + 16);
    mc = signed16(raw + 18);
    md = signed16(raw + 20);
    return ac4 != 0 && ac4 != 65535 && ac5 != 0 && ac6 != 0;
  }
  bool compensate(int32_t ut, int32_t up, double &pressure)
  {
    int64_t x1 = ((int64_t(ut) - ac6) * ac5) >> 15;
    if (x1 + md == 0)
      return false;
    int64_t x2 = (int64_t(mc) * 2048) / (x1 + md), b5 = x1 + x2;
    // Reject nonsensical calibration/conversion before squared arithmetic.
    if (b5 < -100000 || b5 > 100000)
      return false;
    int64_t b6 = b5 - 4000;
    x1 = (int64_t(b2) * ((b6 * b6) >> 12)) >> 11;
    x2 = (int64_t(ac2) * b6) >> 11;
    int64_t x3 = x1 + x2, b3 = (int64_t(ac1) * 4 + x3 + 2) / 4;
    x1 = (int64_t(ac3) * b6) >> 13;
    x2 = (int64_t(b1) * ((b6 * b6) >> 12)) >> 16;
    x3 = (x1 + x2 + 2) >> 2;
    if (x3 + 32768 <= 0 || int64_t(up) < b3)
      return false;
    const uint64_t b4 = (uint64_t(ac4) * uint64_t(x3 + 32768)) >> 15;
    if (b4 == 0)
      return false;
    const uint64_t b7 = uint64_t(int64_t(up) - b3) * 50000;
    int64_t p = b7 < 0x80000000ULL ? (b7 * 2) / b4 : (b7 / b4) * 2;
    if (p < 1000 || p > 120000)
      return false;
    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    p += (x1 + x2 + 3791) >> 4;
    pressure = double(p);
    return pressure >= 1000 && pressure <= 120000;
  }
  bool read(double &pressure)
  {
    uint8_t raw[3];
    if (!writeReg(0x77, 0xf4, 0x2e))
      return false;
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!readRegs(0x77, 0xf6, raw, 2))
      return false;
    const int32_t ut = (uint32_t(raw[0]) << 8) | raw[1];
    if (!writeReg(0x77, 0xf4, 0x34))
      return false;
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!readRegs(0x77, 0xf6, raw, 3))
      return false;
    const int32_t up = ((uint32_t(raw[0]) << 16) | (uint32_t(raw[1]) << 8) | raw[2]) >> 8;
    return compensate(ut, up, pressure);
  }
};
