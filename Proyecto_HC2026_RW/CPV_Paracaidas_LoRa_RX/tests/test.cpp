#include "../RecoveryControl.h"
#include "../Sensors.h"
#include "../FlightPreparation.h"
#include "../RestartControl.h"
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
  assert(decodeCommand("CALIBRAR",8)==Command::Calibrate);
  assert(decodeCommand("CALIBRAR",7)==Command::Unknown);
  assert(decodeCommand("CALIBRARx",9)==Command::Unknown);
  assert(decodeCommand("REINICIAR",9)==Command::Restart);
  RestartControl restart;restart.request(0xfffffff0U);
  assert(!restart.due(0xfffffff0U+750U,true,false));
  restart.acknowledged=true;
  assert(!restart.due(0xfffffff0U+749U,true,false));
  assert(!restart.due(0xfffffff0U+750U,false,false));
  assert(!restart.due(0xfffffff0U+750U,true,true));
  assert(restart.due(0xfffffff0U+750U,true,false));
  restart.cancel();assert(!restart.due(0xfffffff0U+800U,true,false));
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

  ImuSample resting;resting.force={0,0,9.81};
  FlightMonitor preparation;
  for(int i=0;i<500;i++)preparation.update(i*.02,101325,resting,true,true,false,false);
  assert(preparation.flight.t>=0 && !preparation.calibrated && !preparation.calibration.active);
  assert(preparation.epoch==0 && !preparation.flight.fired); // No automatic calibration in ten seconds of rest.
  assert(preparation.startCalibration(10,false,false));
  assert(!preparation.startCalibration(10,false,false));
  for(int i=0;i<101;i++)preparation.update(10+i*.02,101325,resting,true,true,false,false);
  assert(!preparation.calibrated && preparation.calibration.active);
  preparation.update(12.02,101325,resting,true,true,false,false);
  assert(preparation.calibrated && !preparation.calibration.active && preparation.epoch==1);
  assert(preparation.projection.valid);
  // Failed recalibration retains the existing reference; no automatic restart.
  const double reference=preparation.flight.p0;
  assert(preparation.startCalibration(12.1,false,false));
  ImuSample moving=resting;moving.gyro={1,0,0};
  for(int i=0;i<=751;i++)preparation.update(12.1+i*.02,101325,moving,true,true,false,false);
  assert(!preparation.calibration.active && preparation.calibration.failed);
  assert(preparation.calibrated && preparation.epoch==1 && preparation.flight.p0==reference);
  assert(!preparation.startCalibration(28,true,false));
  assert(!preparation.startCalibration(28,false,true));

  // Launch during calibration cancels the candidate BEFORE it can erase flight state.
  FlightMonitor interrupted;assert(interrupted.startCalibration(0,false,false));
  for(int i=0;i<100;i++) {
    const double t=i*.02, pressureAtHeight=101325*std::exp(-(4*t)/(287*293.15/9.81));
    interrupted.update(t,pressureAtHeight,resting,true,true,false,false);
  }
  assert(interrupted.flight.state==1 && interrupted.calibration.failed && !interrupted.calibration.active);
  assert(interrupted.epoch==0 && !interrupted.calibrated && interrupted.flight.h>7);
  assert(!interrupted.startCalibration(2,false,false));
  FlightMonitor releasedDuringCal;assert(releasedDuringCal.startCalibration(0,false,false));
  releasedDuringCal.update(.02,101325,resting,true,true,false,true);
  assert(!releasedDuringCal.calibration.active && releasedDuringCal.calibration.failed);

  ManualCalibration window;window.begin(0);
  for(int i=0;i<90;i++)assert(!window.collect(i*.02,101325,resting,true,true,false));
  assert(!window.collect(1.8,101325,moving,true,true,false) && window.samples>0); // One spike is filtered.
  assert(!window.collect(1.82,101325,moving,true,true,false) && window.samples==0); // Sustained movement resets.
  for(int i=0;i<101;i++)assert(!window.collect(1.84+i*.02,101325,resting,true,true,false));
  assert(window.collect(3.86,101325,resting,true,true,false));

  // Reported failure: a quiet but biased/noisy IMU never passed the old
  // per-reading +/-1 m/s2 and 5 deg/s gates. Isolated spikes must not erase progress.
  FlightMonitor noisyRest;assert(noisyRest.startCalibration(0,false,false));
  const Vec3 restingBias{.12,-.18,11.15}, gyroBias{.105,-.012,.008};
  int oldAccepted=0;
  for(int i=0;i<160 && !noisyRest.calibrated;i++) {
    const double t=i*.02;
    ImuSample noisy;
    noisy.force=restingBias+Vec3{.12*std::sin(i*1.7),.1*std::cos(i*.9),.18*std::sin(i*1.1)};
    noisy.gyro=gyroBias+Vec3{.01*std::sin(i*1.3),.008*std::cos(i*.7),.008*std::sin(i*1.9)};
    if(i%17==8){noisy.force.z+=4;noisy.gyro.x+=.5;}
    if(std::abs(noisy.force.norm()-9.81)<1 && noisy.gyro.norm()<.0873)++oldAccepted;
    noisyRest.update(t,101325+2*std::sin(i*.7),noisy,true,true,false,false);
  }
  assert(oldAccepted==0);
  assert(noisyRest.calibrated && noisyRest.epoch==1 && !noisyRest.calibration.failed);
  assert(noisyRest.flight.state==0 && !noisyRest.flight.fired);
  assert(std::abs(noisyRest.flight.p0-101325)<1);
  assert((noisyRest.projection.gyroBias-gyroBias).norm()<.006);
  assert(std::abs(noisyRest.projection.forceVertical(restingBias)-9.81)<.08);

  // Varying orientation/rotation are movement, even within the absolute bounds.
  for(int scenario=0;scenario<2;scenario++) {
    ManualCalibration shaking;shaking.begin(0);
    for(int i=0;i<=750;i++) {
      const double phase=2*3.141592653589793*i*.02;
      ImuSample motion=resting;
      if(scenario==0) {
        const double angle=.2*std::sin(phase);
        motion.force={9.81*std::sin(angle),0,9.81*std::cos(angle)};
      } else motion.gyro={.1*std::sin(phase),0,0};
      assert(!shaking.collect(i*.02,101325,motion,true,true,false));
    }
    assert(shaking.failed && !shaking.active);
  }
  // Bad readings, saturation and long gaps cannot complete a reference.
  for(int scenario=0;scenario<4;scenario++) {
    ManualCalibration invalid;invalid.begin(0);
    for(int i=0;i<60;i++)assert(!invalid.collect(i*.02,101325,resting,true,true,false));
    ImuSample bad=resting;
    if(scenario==0)bad.accelClipped=true;
    if(scenario==1)bad.gyro.x=std::numeric_limits<double>::quiet_NaN();
    const double badPressure=scenario==2 ? std::numeric_limits<double>::quiet_NaN() : 101325;
    assert(!invalid.collect(scenario==3 ? 2 : 1.2,badPressure,bad,true,true,false));
    assert(invalid.samples==0);
  }

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
  FlightLogic flight,backup;FlightMonitor unprepared;double trigger=-1,fallback=-1,unpreparedTrigger=-1;
  for(int i=0;i<int(samples.size());i++){
    const auto s=samples[i];const double p=101325*std::exp(-s.h/(287*293.15/9.81)),f=derivative(v,i)+9.81;
    assert(flight.step(s.t,p,f));assert(backup.step(s.t,p,0,false));
    unprepared.update(s.t,p,resting,false,true,false,false);
    if(unprepared.flight.fired&&unpreparedTrigger<0)unpreparedTrigger=s.t;
    if(flight.fired&&trigger<0)trigger=s.t;
    if(backup.fired&&fallback<0)fallback=s.t;
  }
  assert(std::abs(trigger-12.78)<.001);assert(fallback>14.74&&fallback<15.74);
  assert(unpreparedTrigger==fallback && !unprepared.calibrated && unprepared.epoch==0);
  std::cout<<"PASS manual preparation: filtered rest with bias/noise/spikes, motion/scatter rejection, invalid data/gaps, explicit completion, timeout, launch/release preemption, unarmed/uncalibrated barometric flight.\n";
  std::cout<<"PASS: protocol, motor directions/timing/preemption/wrap, orientation, BMP180 example, filter replay, sensor dropout.\n";
  std::cout<<"Automatic order "<<trigger<<" s; barometer-only fallback "<<fallback<<" s.\n";
}
