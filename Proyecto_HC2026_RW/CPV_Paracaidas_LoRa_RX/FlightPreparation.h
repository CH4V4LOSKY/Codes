#pragma once
#include "Sensors.h"

// Manual preparation is independent of flight detection and motor arming.
// A candidate reference never replaces the working reference until complete.
struct ManualCalibration
{
  struct Reading { double pressure; Vec3 force, gyro; };
  static constexpr double Timeout = 15.0, WindowSeconds = 1.98;
  // Check scatter separately from bias: calibration exists to measure bias.
  static constexpr double MaxForceRms = .65, MaxGyroRms = .035;
  static constexpr double MaxMeanGyro = .174533; // 10 deg/s before bias correction.
  bool active = false, failed = false;
  unsigned samples = 0;
  double started = 0, firstSample = 0, lastSample = -1, sumPressure = 0;
  Vec3 sumForce{}, sumGyro{};
  double forceM2 = 0, gyroM2 = 0;
  Reading recent[3]{};
  unsigned recentCount = 0, recentNext = 0;
  static double median(double a, double b, double c)
  {
    if (a > b) { const double tmp = a; a = b; b = tmp; }
    if (b > c) b = c;
    return a > b ? a : b;
  }
  static Vec3 median(Vec3 a, Vec3 b, Vec3 c)
  {
    return {median(a.x, b.x, c.x), median(a.y, b.y, c.y), median(a.z, b.z, c.z)};
  }
  void resetSamples()
  {
    samples = 0;
    lastSample = -1;
    sumPressure = 0;
    sumForce = sumGyro = {};
    forceM2 = gyroM2 = 0;
    recentCount = recentNext = 0;
  }
  void begin(double time)
  {
    active = true;
    failed = false;
    started = time;
    resetSamples();
  }
  void cancel() { active = false; failed = true; resetSamples(); }
  bool collect(double time, double pressure, const ImuSample &imu, bool imuOk, bool baroOk, bool blocked)
  {
    if (!active) return false;
    if (blocked || time - started >= Timeout) { cancel(); return false; }
    const bool valid = imuOk && baroOk && !imu.accelClipped && !imu.gyroClipped &&
                            std::isfinite(time) && time >= started &&
                            std::isfinite(pressure) && pressure >= 1000 && pressure <= 120000 &&
                            std::isfinite(imu.force.norm()) && std::isfinite(imu.gyro.norm());
    if (!valid) { resetSamples(); return false; }
    if (lastSample >= 0 && time <= lastSample) { resetSamples(); return false; }
    if (lastSample >= 0 && time - lastSample > .1) resetSamples();
    lastSample = time;
    recent[recentNext] = {pressure, imu.force, imu.gyro};
    recentNext = (recentNext + 1) % 3;
    if (recentCount < 3) ++recentCount;
    if (recentCount < 3) return false;

    // A three-reading median removes isolated spikes without smoothing a
    // sustained movement into apparent rest. This filter is calibration-only.
    const Vec3 force = median(recent[0].force, recent[1].force, recent[2].force);
    const Vec3 gyro = median(recent[0].gyro, recent[1].gyro, recent[2].gyro);
    const double gravity = force.norm();
    if (gravity < 7.5 || gravity > 12.5 || gyro.norm() > MaxMeanGyro) {
      resetSamples();
      return false;
    }
    if (!samples) firstSample = time;
    const Vec3 oldForce = samples ? sumForce * (1.0 / samples) : Vec3{};
    const Vec3 oldGyro = samples ? sumGyro * (1.0 / samples) : Vec3{};
    sumForce = sumForce + force;
    sumGyro = sumGyro + gyro;
    sumPressure += median(recent[0].pressure, recent[1].pressure, recent[2].pressure);
    ++samples;
    const Vec3 meanForce = sumForce * (1.0 / samples), meanGyro = sumGyro * (1.0 / samples);
    forceM2 += (force - oldForce).dot(force - meanForce);
    gyroM2 += (gyro - oldGyro).dot(gyro - meanGyro);
    if (samples < 100 || time - firstSample < WindowSeconds - 1e-8) return false;
    const bool stable = meanForce.norm() >= 8 && meanForce.norm() <= 12 &&
                        meanGyro.norm() <= MaxMeanGyro &&
                        forceM2 / samples <= MaxForceRms * MaxForceRms &&
                        gyroM2 / samples <= MaxGyroRms * MaxGyroRms;
    if (!stable) resetSamples();
    return stable;
  }
};

struct FlightMonitor
{
  FlightLogic flight;
  VerticalProjection projection;
  ManualCalibration calibration;
  bool calibrated = false, forceValid = false;
  unsigned epoch = 0;
  double lastImuTime = -1, lastGoodForce = 0;

  bool startCalibration(double time, bool closing, bool deployed)
  {
    if (calibration.active || flight.state != 0 || closing || deployed) return false;
    calibration.begin(time);
    return true;
  }
  void update(double time, double pressure, const ImuSample &imu, bool imuOk, bool baroOk,
              bool closing, bool deployed)
  {
    const double dt = time - lastImuTime;
    lastImuTime = time;
    if (!imuOk || imu.gyroClipped) projection.valid = false;
    else if (projection.valid) projection.update(imu.gyro, dt);
    forceValid = calibrated && imuOk && !imu.accelClipped && projection.valid && !closing;
    if (!forceValid) lastGoodForce = time;
    forceValid = forceValid && time - lastGoodForce >= .5;

    // Always run the original flight logic. The FIRST valid pressure establishes
    // a basic reference; no ARMAR or manual calibration permission is required.
    // Without manual orientation calibration, use the existing barometric path.
    if (baroOk) flight.step(time, pressure, forceValid ? projection.forceVertical(imu.force) : 0, forceValid);

    // Detect launch BEFORE deciding whether to commit a new calibration.
    if (calibration.collect(time, pressure, imu, imuOk, baroOk, flight.state != 0 || closing || deployed))
    {
      VerticalProjection candidate;
      const double scale = 1.0 / calibration.samples;
      if (candidate.initialize(calibration.sumForce * scale, calibration.sumGyro * scale))
      {
        projection = candidate;
        flight = FlightLogic();
        flight.step(time, calibration.sumPressure * scale, 9.81);
        calibrated = true;
        forceValid = false;
        lastGoodForce = time;
        ++epoch; // Reset peaks/UI only on a completed, grounded calibration.
        calibration.active = false;
        calibration.failed = false;
      }
      else calibration.cancel();
    }
  }
};
