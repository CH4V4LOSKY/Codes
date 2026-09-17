#include <cassert>
#include <fstream>
#include <iostream>
#include "SPI.h"
#include "LoRa.h"
#include "Servo.h"
// Compile the original sketches unchanged with fake IO; execute their loops.
#define setup stationSetup
#define loop stationLoop
#include "../../Proyecto_HC2026_RW/EstacionTerrena_LoRa_Lib/EstacionTerrena_LoRa_Lib.ino"
#undef setup
#undef loop
#define setup fredySetup
#define loop fredyLoop
#include "../../Proyecto_HC2026_RS/code_Fredy/code_Fredy.ino"
#undef setup
#undef loop
int main(){
  stationSetup();const auto stationConfig=LoRa.config;LoRa.config.clear();
  fredySetup();assert(LoRa.config==stationConfig);assert(servo.pin==5&&servo.position==0);
  std::ifstream file("build/ui-command-tests/ui-bytes.txt",std::ios::binary);assert(file.good());
  char c;while(file.get(c))Serial.input.push_back(c);
  stationLoop();assert(LoRa.sent.size()==2);assert(LoRa.sent[0]=="ARMAR"&&LoRa.sent[1]=="ACTIVAR");
  for(size_t i=0;i<LoRa.sent.size();i++){LoRa.incoming.push_back(LoRa.sent[i]);fredyLoop();assert(servo.position==(i==0?0:90));}
  assert(Serial.output.find("Enviado: ARMAR\n")!=std::string::npos);
  assert(Serial.output.find("Enviado: ACTIVAR\n")!=std::string::npos);
  LoRa.incoming.push_back("CMD:3:ARMAR");fredyLoop();assert(servo.position==90);
  // CRLF should generate one payload, despite two line-ending characters.
  for(char b:std::string(" armar \r\n"))Serial.input.push_back(b);
  stationLoop();assert(LoRa.sent.size()==3&&LoRa.sent.back()=="ARMAR");
  std::cout<<"PASS: UI bytes -> original station loop -> LoRa payload -> original Fredy loop -> servo requests 0/90. Radio settings match.\n";
}
