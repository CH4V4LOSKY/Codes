#pragma once
#include "Arduino.h"
struct TestRadio {
  bool receiving=false, stuck=false;
  uint32_t busyUntil=0;
  unsigned receives=0;
  std::vector<uint8_t> tx,rx;
  size_t index=0;
  std::vector<std::vector<uint8_t>> sent;
  std::vector<uint32_t> times;
  void setPins(int,int,int){}
  bool begin(long){return true;}
  void setSpreadingFactor(int){}
  void setSignalBandwidth(double){}
  void setCodingRate4(int){}
  void setSyncWord(int){}
  void setPreambleLength(int){}
  void enableCrc(){}
  void disableInvertIQ(){}
  void receive(){receiving=true;++receives;}
  void idle(){busyUntil=0;stuck=false;}
  bool beginPacket(){if(stuck || testClock<busyUntil)return false;receiving=false;tx.clear();return true;}
  void write(const uint8_t *data,size_t n){tx.assign(data,data+n);}
  void endPacket(bool async){if(!async)throw "blocking TX";sent.push_back(tx);times.push_back(testClock);busyUntil=testClock+170;}
  int parsePacket(){receiving=false;index=0;return int(rx.size());}
  int available(){return int(rx.size()-index);}
  int read(){return rx[index++];}
  int packetRssi(){return -81;}
  float packetSnr(){return 7.25f;}
  template<class T> void inject(const T &packet){if(!receiving)return;const auto p=reinterpret_cast<const uint8_t *>(&packet);rx.assign(p,p+sizeof(packet));index=0;}
};
inline TestRadio LoRa;
inline int digitalRead(int){return LoRa.receiving && LoRa.index<LoRa.rx.size() ? HIGH : LOW;}
