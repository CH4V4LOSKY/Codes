#pragma once
#include "SPI.h"
#include <vector>
#include <map>
struct RadioStub {
  std::map<std::string,long> config;
  std::vector<std::string> sent;
  std::deque<std::string> incoming;
  std::string tx,rx;size_t index=0;
  void setPins(int,int,int){}
  int begin(long n){config["frequency"]=n;return 1;}
  void setSpreadingFactor(int n){config["sf"]=n;}
  void setSignalBandwidth(long n){config["bw"]=n;}
  void setCodingRate4(int n){config["cr"]=n;}
  void setSyncWord(int n){config["sync"]=n;}
  void setPreambleLength(int n){config["preamble"]=n;}
  void disableCrc(){config["crc"]=0;}
  void disableInvertIQ(){config["invert"]=0;}
  void beginPacket(){tx.clear();}
  void print(const String &s){tx+=s.value;}
  void endPacket(){sent.push_back(tx);}
  int parsePacket(){if(incoming.empty())return 0;rx=incoming.front();incoming.pop_front();index=0;return int(rx.size());}
  int available(){return int(rx.size()-index);}
  int read(){return rx[index++];}
};
inline RadioStub LoRa;
