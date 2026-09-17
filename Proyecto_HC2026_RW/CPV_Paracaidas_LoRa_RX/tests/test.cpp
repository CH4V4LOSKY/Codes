#include "../RecoveryControl.h"
#include "../Sensors.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>
#include <algorithm>
#include <limits>

int main(int argc,char **argv){
  assert(decodeCommand("ARMAR",5)==Command::Arm);
  assert(decodeCommand("ACTIVAR",7)==Command::Release);
  assert(decodeCommand("CMD:3:ACTIVAR",13)==Command::Unknown);
  assert(decodeCommand("ACTIVARx",8)==Command::Unknown);
  assert(decodeCommand("ACTIVAR",6)==Command::Unknown);
  assert(decodeCommand("activar",7)==Command::Unknown);
  RecoveryControl motor;
  assert(motor.arm(100,false));assert(motor.direction==RecoveryControl::Closing);
  assert(!motor.arm(110,false));motor.tick(1099);assert(motor.direction==RecoveryControl::Closing);
  motor.tick(1100);assert(motor.direction==RecoveryControl::Off);
  assert(!motor.arm(1200,true));
  assert(motor.release(2000));assert(motor.direction==RecoveryControl::Opening);
  assert(!motor.release(3000));motor.tick(6999);assert(motor.direction==RecoveryControl::Opening);
  motor.tick(7000);assert(motor.direction==RecoveryControl::Off);
  assert(!motor.arm(8000,false));assert(!motor.release(8000));
  assert(motor.release(8000,true));assert(!motor.release(8100,true));
  motor.tick(13000);assert(motor.direction==RecoveryControl::Off);
  RecoveryControl preempt;preempt.arm(0,false);assert(preempt.release(300));
  assert(preempt.direction==RecoveryControl::Off);preempt.tick(319);assert(preempt.direction==RecoveryControl::Off);
  preempt.tick(320);assert(preempt.direction==RecoveryControl::Opening);
  preempt.tick(5320);assert(preempt.direction==RecoveryControl::Off);
  RecoveryControl wrap;const uint32_t start=0xfffffff0U;wrap.release(start);
  wrap.tick(start+4999U);assert(wrap.direction==RecoveryControl::Opening);
  wrap.tick(start+5000U);assert(wrap.direction==RecoveryControl::Off);

  VerticalProjection projection;
  assert(projection.initialize({0,0,9.81},{0,0,0}));
  for(int i=0;i<50;i++)assert(projection.update({0,1.5707963267948966,0},.02));
  assert(std::abs(projection.up.x+1)<1e-9);assert(std::abs(projection.up.z)<1e-9);
  assert(std::abs(projection.forceVertical({-9.81,0,0})-9.81)<1e-9);
  assert(!projection.update({0,0,0},.2));assert(!projection.valid);

  Bmp180 bmp;bmp.ac1=408;bmp.ac2=-72;bmp.ac3=-14383;bmp.ac4=32741;bmp.ac5=32757;bmp.ac6=23153;
  bmp.b1=6190;bmp.b2=4;bmp.mc=-8711;bmp.md=2868;double pressure;
  assert(bmp.compensate(27898,23843,pressure));assert(std::abs(pressure-69964)<=1);
  bmp.ac4=0;assert(!bmp.compensate(27898,23843,pressure));

  FlightLogic stationary;for(int i=0;i<500;i++)stationary.step(i*.02,101325,9.81);
  assert(!stationary.fired);assert(std::abs(stationary.vr)<1e-9);
  assert(!stationary.step(10,std::numeric_limits<double>::quiet_NaN(),9.81));
  assert(!stationary.step(0,101325,9.81));
  stationary.step(11,101325,9.81);assert(stationary.eta<0);
  assert(stationary.step(11.02,101325,9.81)); // Recovers after a missing interval.

  assert(argc==2);std::ifstream file(argv[1]);assert(file.good());
  struct Point{double t,h;};std::vector<Point> raw;std::string line;bool active=false;
  while(std::getline(file,line)){
    if(line.find("EphemerisLLATimePos")!=std::string::npos){active=true;continue;}
    if(!active)continue;double t,lat,lon,h;std::istringstream row(line);
    if(row>>t>>lat>>lon>>h)raw.push_back({t,h});
  }
  assert(raw.size()==2229);const double base=raw.front().h;
  std::vector<Point> samples;size_t j=0;
  for(int i=0;i*.02<=raw.back().t+1e-8;i++){
    const double t=i*.02;while(j+1<raw.size()-1&&raw[j+1].t<t)++j;
    const double w=(t-raw[j].t)/(raw[j+1].t-raw[j].t);
    samples.push_back({t,raw[j].h+w*(raw[j+1].h-raw[j].h)-base});
  }
  auto derivative=[](const std::vector<double>&v,int i){int a=std::max(0,i-5),b=std::min(int(v.size())-1,i+5);return (v[b]-v[a])/((b-a)*.02);};
  std::vector<double> h,v;for(auto s:samples)h.push_back(s.h);
  for(int i=0;i<int(h.size());i++)v.push_back(derivative(h,i));
  FlightLogic flight,backup;double trigger=-1,fallback=-1;
  for(int i=0;i<int(samples.size());i++){
    const auto s=samples[i];const double p=101325*std::exp(-s.h/(287*293.15/9.81)),f=derivative(v,i)+9.81;
    assert(flight.step(s.t,p,f));assert(backup.step(s.t,p,0,false));
    if(flight.fired&&trigger<0)trigger=s.t;
    if(backup.fired&&fallback<0)fallback=s.t;
  }
  assert(std::abs(trigger-12.78)<.001);assert(fallback>14.74&&fallback<15.74);
  std::cout<<"PASS: protocol, motor directions/timing/preemption/wrap, orientation, BMP180 example, filter replay, sensor dropout.\n";
  std::cout<<"Automatic order "<<trigger<<" s; barometer-only fallback "<<fallback<<" s.\n";
}
