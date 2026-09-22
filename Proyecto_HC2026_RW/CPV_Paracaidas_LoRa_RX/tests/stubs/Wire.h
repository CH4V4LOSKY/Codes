#pragma once
#include <stdint.h>
// Compile-only I2C substitute. All reads fail; never represents real hardware.
struct TestWire {
  void begin(int,int){}
  void setClock(unsigned){}
  void setTimeOut(unsigned){}
  void beginTransmission(uint8_t){}
  void write(uint8_t){}
  int endTransmission(bool=true){return 1;}
  int requestFrom(uint8_t,uint8_t){return 0;}
  int read(){return -1;}
};
inline TestWire Wire;
