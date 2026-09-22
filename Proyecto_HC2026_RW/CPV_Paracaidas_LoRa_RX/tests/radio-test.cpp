#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <atomic>
#include "../Sensors.h"
#include "../RadioProtocol.h"
#include <cassert>
#include <iostream>
#include <fstream>
namespace CPV {
#include "../CPV_Paracaidas_LoRa_RX.ino"
}
namespace Ground {
#include "../EstacionTerrena/EstacionTerrena.ino"
}
template<class F> void run(F task,uint32_t until) {
  stopAt=until;
  try {task(nullptr);} catch(const StopTask &) {}
}
int main() {
  using namespace RadioProtocol;
  CommandPacket packet{CommandMagic,10,1,2},decoded;
  assert(decodePacket(&packet,sizeof(packet),decoded)==Command::Release);
  assert(decodePacket(&packet,sizeof(packet)-1,decoded)==Command::Unknown);
  packet.command=99;assert(decodePacket(&packet,sizeof(packet),decoded)==Command::Unknown);packet.command=2;
  CommandHistory cache;assert(cache.find(packet)==None);cache.remember(packet,Accepted);assert(cache.find(packet)==Accepted);
  auto forged=packet;forged.command=1;assert(cache.find(forged)==Ignored);
  PendingCommand pending;
  assert(pending.begin(Command::Arm,10,1));
  assert(!pending.begin(Command::Arm,10,2));
  assert(pending.begin(Command::Release,10,2));
  assert(!pending.begin(Command::Arm,10,3));
  assert(!pending.begin(Command::Release,10,3));
  assert(!pending.begin(Command::Calibrate,10,3)); // Calibration cannot preempt deployment.
  PendingCommand calibrating;assert(calibrating.begin(Command::Calibrate,10,7));
  assert(calibrating.packet.command==3);
  assert(!calibrating.begin(Command::Calibrate,10,8));
  assert(!calibrating.begin(Command::Arm,10,8));
  assert(calibrating.begin(Command::Release,10,8));
  const uint32_t wrap=0xfffffff0U;
  pending.sent(wrap,0);assert(!pending.due(wrap+699));assert(pending.due(wrap+700));
  for(int n=1;n<8;n++)pending.sent(wrap+uint32_t(n)*700,0);
  assert(pending.expired());
  Telemetry ack;ack.ackSession=10;ack.ackSequence=9;ack.ackCommand=2;ack.ackResult=Accepted;
  assert(!pending.acknowledge(ack));ack.ackSequence=2;assert(pending.acknowledge(ack));

  // Execute the actual CPV radio/motor task against a half-duplex radio stub.
  CPV::setup();
  Telemetry snapshot;snapshot.maxHeight=1234;snapshot.maxSpeed=80;snapshot.maxAcceleration=44;
  xQueueOverwrite(CPV::telemetryQueue,&snapshot);
  onTick=[&] {
    if(testClock==250)LoRa.inject(CommandPacket{CommandMagic,10,1,1});
    if(testClock==350 || testClock==1350 || testClock==6350)LoRa.inject(CommandPacket{CommandMagic,10,2,2});
    if(testClock==7350)LoRa.inject(CommandPacket{CommandMagic,10,3,2});
  };
  run(CPV::radioTask,12600);
  std::vector<uint32_t> opening,off;
  for(auto [time,pin,value]:pinChanges)if(pin==25)(value?opening:off).push_back(time);
  assert((opening==std::vector<uint32_t>{370,7350}));
  assert((off==std::vector<uint32_t>{5370,12350}));
  assert(LoRa.sent.size()==26);
  for(size_t n=0;n<LoRa.sent.size();n++) {
    Telemetry t;assert(decodeTelemetry(LoRa.sent[n].data(),LoRa.sent[n].size(),t));
    assert(t.sequence==n && t.maxHeight==1234);
    if(n>0)assert(LoRa.times[n]-LoRa.times[n-1]==500);
    if(n>=1)assert((t.flags&Deployed) && t.source==Manual);
    if(n>=13 && n<15)assert(!(t.flags&MotorOpening)); // Late duplicate did not reopen.
    if(n>=1)assert(std::isnan(t.height)); // Missing/stale sensors do not fabricate readings.
  }
  // Automatic activation is also included in telemetry, independent of sensors.
  LoRa=TestRadio{};testClock=0;CPV::released=false;
  onTick=[] {if(testClock==250)CPV::requestRelease=true;};run(CPV::radioTask,600);
  Telemetry automatic;assert(!LoRa.sent.empty());assert(decodeTelemetry(LoRa.sent.back().data(),100,automatic));
  assert((automatic.flags&Deployed) && automatic.source==Automatic);
  // Sensor task continues to publish even AFTER deployment with failed I2C.
  testClock=0;CPV::released=true;onTick={};run(CPV::sensorTask,2100);
  Telemetry missing;xQueuePeek(CPV::telemetryQueue,&missing,0);
  assert(missing.uptime>=2000 && !(missing.flags&(ImuOk|BaroOk)));

  // Execute the station task: first ACK lost, retry same identity, then stop.
  LoRa=TestRadio{};testClock=0;Ground::setup();
  Command cmd=Command::Release;xQueueSend(Ground::inputQueue,&cmd,0);
  onTick=[&] {
    if(testClock==250 || testClock==1250) {
      Telemetry t;t.boot=77;t.sequence=testClock/500;t.flags=Calibrated|Deployed|BaroOk;
      t.source=Manual;t.height=900;t.speed=-5;t.maxHeight=1234;t.maxSpeed=80;t.maxAcceleration=44;
      if(testClock==1250) {t.ackSession=12345;t.ackSequence=1;t.ackCommand=2;t.ackResult=Accepted;}
      LoRa.inject(t);
    }
  };
  run(Ground::stationRadioTask,2500);
  assert(LoRa.sent.size()==2 && LoRa.sent[0]==LoRa.sent[1]);
  bool gotAck=false,gotTelemetry=false;Ground::UsbLine line;
  std::ofstream fixture("build/cpv-rx-tests/station-telemetry.txt");
  while(xQueueReceive(Ground::outputQueue,&line,0)) {
    const std::string s=line.text;
    gotAck|=s.find("UI_CMD,ACK,12345,1,ACTIVAR,1")==0;
    if(s.find("UI_TLM2,")==0){gotTelemetry=true;fixture<<s<<'\n';}
  }
  assert(gotAck && gotTelemetry);
  fixture.close();

  // Actual CPV task: ARMAR completes without requesting calibration; the next
  // CALIBRAR is separate, deduplicated, and blocked after flight detection.
  LoRa=TestRadio{};testClock=0;CPV::released=false;CPV::airborne=false;
  CPV::requestRelease=false;CPV::calibrationRequested=0;CPV::calibrationHandled=0;
  onTick=[] {
    if(testClock==250)LoRa.inject(CommandPacket{CommandMagic,12,1,1});
    if(testClock==350)LoRa.inject(CommandPacket{CommandMagic,12,2,3}); // Rejected while closing.
    if(testClock==1700)assert(CPV::calibrationRequested.load()==0);
    if(testClock==1750 || testClock==2250)LoRa.inject(CommandPacket{CommandMagic,12,3,3});
    if(testClock==2700) {CPV::calibrationHandled=CPV::calibrationRequested.load();CPV::airborne=true;}
    if(testClock==2750)LoRa.inject(CommandPacket{CommandMagic,12,4,3});
  };
  run(CPV::radioTask,3200);
  assert(CPV::calibrationRequested.load()==1);
  bool armedSeen=false,calibrationSeen=false,rejectedInFlight=false;
  for(const auto &bytes:LoRa.sent) {
    Telemetry t;assert(decodeTelemetry(bytes.data(),bytes.size(),t));
    if(t.uptime==1500)armedSeen=(t.flags&Armed) && !(t.flags&Calibrating);
    if(t.uptime==2000)calibrationSeen=(t.flags&Armed) && (t.flags&Calibrating) && t.ackCommand==3 && t.ackResult==Accepted;
    if(t.uptime==3000)rejectedInFlight=t.ackSequence==4 && t.ackResult==Ignored;
  }
  assert(armedSeen && calibrationSeen && rejectedInFlight);

  // USB CALIBRAR traverses the real station serial parser and radio protocol.
  LoRa=TestRadio{};testClock=0;onTick={};Ground::setup();
  for(char c:std::string(" calibrar \r\n"))Serial.input.push_back(c);
  stopAt=100;Ground::loop();
  onTick=[] {
    if(testClock==251) {
      Telemetry t;t.ackSession=12345;t.ackSequence=1;t.ackCommand=3;t.ackResult=Accepted;
      LoRa.inject(t);
    }
  };
  run(Ground::stationRadioTask,600);
  assert(LoRa.sent.size()==1);
  assert(decodePacket(LoRa.sent.front().data(),LoRa.sent.front().size(),decoded)==Command::Calibrate);
  bool calibrationAck=false;
  while(xQueueReceive(Ground::outputQueue,&line,0))
    calibrationAck|=std::string(line.text).find("UI_CMD,ACK,12345,1,CALIBRAR,1")==0;
  assert(calibrationAck);

  // Real CPV task: announce reset before reboot, all motor outputs LOW.
  auto freshCpv=[] {
    LoRa=TestRadio{};testClock=0;testRandom=12345;onTick={};
    CPV::released=false;CPV::airborne=false;CPV::closingMotor=false;CPV::requestRelease=false;
    CPV::calibrationRequested=0;CPV::calibrationHandled=0;CPV::setup();
  };
  freshCpv();
  onTick=[] {if(testClock==250)LoRa.inject(CommandPacket{CommandMagic,20,1,4,12345});};
  try {run(CPV::radioTask,1600);assert(false);} catch(const TestRestart &) {}
  assert(testRestartCount==1 && testRestartAt==1000 && !pins[25] && !pins[26]);
  Telemetry rebootAck;assert(decodeTelemetry(LoRa.sent.back().data(),100,rebootAck));
  assert(rebootAck.ackCommand==4 && rebootAck.ackResult==Accepted && (rebootAck.flags&RestartPending));

  // A retry tied to the PREVIOUS boot cannot reboot the new instance again.
  freshCpv();testRandom=67891;
  onTick=[] {if(testClock==250 || testClock==1250)LoRa.inject(CommandPacket{CommandMagic,20,1,4,12345});};
  run(CPV::radioTask,1800);assert(testRestartCount==1);
  assert(decodeTelemetry(LoRa.sent.back().data(),100,rebootAck));
  assert(rebootAck.ackResult==Ignored && !(rebootAck.flags&RestartPending));

  // Reset waits for the complete five-second opening pulse.
  freshCpv();
  onTick=[] {
    if(testClock==250)LoRa.inject(CommandPacket{CommandMagic,20,2,2});
    if(testClock==750)LoRa.inject(CommandPacket{CommandMagic,20,3,4,12345});
  };
  try {run(CPV::radioTask,6000);assert(false);} catch(const TestRestart &) {}
  assert(testRestartCount==2 && testRestartAt==5250 && !pins[25] && !pins[26]);
  // Manual AND automatic release requests cancel a pending reboot.
  for(bool automaticRelease:{false,true}) {
    freshCpv();
    onTick=[automaticRelease] {
      if(testClock==250)LoRa.inject(CommandPacket{CommandMagic,20,4,4,12345});
      if(testClock==350) {
        if(automaticRelease)CPV::requestRelease=true;
        else LoRa.inject(CommandPacket{CommandMagic,20,5,2});
      }
    };
    run(CPV::radioTask,6000);assert(testRestartCount==2 && CPV::released.load());
  }

  // Station: no known CPV boot means no reset transmission.
  LoRa=TestRadio{};testClock=0;testRandom=12345;onTick={};Ground::setup();
  for(char c:std::string("reiniciar\n"))Serial.input.push_back(c);
  stopAt=100;Ground::loop();run(Ground::stationRadioTask,1000);
  assert(LoRa.sent.empty());
  bool noDestination=false;
  while(xQueueReceive(Ground::outputQueue,&line,0))noDestination|=std::string(line.text).find("UI_CMD,SIN_DESTINO,")==0;
  assert(noDestination);

  // Lost reset ACK: a new boot confirms completion and stops retries.
  LoRa=TestRadio{};testClock=0;Ground::setup();
  onTick=[] {
    if(testClock==250 || testClock==1500) {Telemetry t;t.boot=testClock==250?111:222;LoRa.inject(t);}
    if(testClock==300) {Command reset=Command::Restart;xQueueSend(Ground::inputQueue,&reset,0);}
  };
  run(Ground::stationRadioTask,3000);
  assert(LoRa.sent.size()==2 && LoRa.sent[0]==LoRa.sent[1]);
  assert(decodePacket(LoRa.sent[0].data(),LoRa.sent[0].size(),decoded)==Command::Restart && decoded.targetBoot==111);
  bool newBootConfirmed=false;
  while(xQueueReceive(Ground::outputQueue,&line,0))newBootConfirmed|=std::string(line.text).find("UI_CMD,REINICIADO,")==0;
  assert(newBootConfirmed);
  std::cout<<"PASS remote reboot: ACK before restart, full motor pulse, stale retry ignored, release priority, missing destination and new-boot confirmation after lost ACK.\n";
  std::cout<<"PASS calibration command cascade: separate ARMAR, closing/flight guards, identity and CALIBRAR ACK.\n";
  std::cout<<"PASS actual firmware tasks: 2 Hz after release, motor 20 ms reversal / 5 s pulse, deduplicated retries, new manual retry, automatic status, sensor failures, station ACK cascade.\n";
}
