#pragma once
#include <cmath>

// Adapted from SIM/CPV_Paracaidas_SIM/FlightLogic.h for real sensor validity.
struct FlightLogic
{
  double tau = .4, accelTau = .1, launchHeight = 3, launchSpeed = 3;
  double descentSpeed = .5, drop = .5, hold = .15, lead = 2;
  double t = -1, p0 = 0, h = 0, vr = 0, vb = 0, va = 0, az = 0, af = 0, eta = -1, peak = 0, since = -1;
  int state = 0;
  bool fired = false, previousForceValid = false;
  bool step(double time, double pressure, double force, bool forceValid = true)
  {
    if (!std::isfinite(time) || !std::isfinite(pressure) || pressure < 1000 || pressure > 120000 || time < 0)
      return false;
    forceValid = forceValid && std::isfinite(force);
    if (t >= 0 && time <= t)
      return false;
    const double a = forceValid ? force - 9.81 : 0;
    if (t < 0)
    {
      p0 = pressure;
      t = time;
      az = af = a;
      previousForceValid = forceValid;
      return true;
    }
    const double dt = time - t, height = (287.0 * 293.15 / 9.81) * std::log(p0 / pressure);
    // Recover from a barometer outage without inventing an interval velocity.
    if (dt > .25)
    {
      t = time;
      h = height;
      vr = vb = va = 0;
      eta = -1;
      since = -1;
      previousForceValid = false;
      return true;
    }
    vb = (height - h) / dt;
    va = vr + (az + a) * .5 * dt;
    const bool inertial = forceValid && previousForceValid;
    const double alpha = tau / (tau + dt);
    vr = inertial ? alpha * va + (1 - alpha) * vb : vb;
    if (forceValid)
    {
      if (previousForceValid)
        af += dt / (accelTau + dt) * (a - af);
      else
        af = a;
    }
    az = a;
    h = height;
    t = time;
    previousForceValid = forceValid;
    if (h > peak)
      peak = h;
    eta = inertial && vr > 0 && af < -.1 ? -vr / af : -1;
    if (state == 0 && h >= launchHeight && vr >= launchSpeed)
      state = 1;
    if (state == 1)
    {
      const bool descent = vr < -descentSpeed && peak - h >= drop;
      if (!descent)
        since = -1;
      else if (since < 0)
        since = t;
      if ((eta > 0 && eta <= lead) || (since >= 0 && t - since + 1e-8 >= hold))
      {
        state = 2;
        fired = true;
      }
    }
    return true;
  }
};

struct Vec3
{
  double x = 0, y = 0, z = 0;
  Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
  Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
  Vec3 operator*(double k) const { return {x * k, y * k, z * k}; }
  double dot(Vec3 b) const { return x * b.x + y * b.y + z * b.z; }
  Vec3 cross(Vec3 b) const { return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x}; }
  double norm() const { return std::sqrt(dot(*this)); }
};

// Local upward unit vector expressed in body axes. Yaw is unnecessary for
// vertical projection. Integrates -omega x up using Rodrigues' rotation.
struct VerticalProjection
{
  Vec3 up{0, 0, 1}, gyroBias{};
  double forceOffset = 0;
  bool valid = false;
  bool initialize(Vec3 meanForce, Vec3 meanGyro)
  {
    const double n = meanForce.norm();
    if (!std::isfinite(n) || n < 8 || n > 12)
      return false;
    up = meanForce * (1 / n);
    gyroBias = meanGyro;
    forceOffset = n - 9.81;
    valid = true;
    return true;
  }
  bool update(Vec3 gyro, double dt)
  {
    if (!valid || !std::isfinite(dt) || dt <= 0 || dt > .1)
    {
      valid = false;
      return false;
    }
    const Vec3 w = gyro - gyroBias;
    const double speed = w.norm();
    if (!std::isfinite(speed))
    {
      valid = false;
      return false;
    }
    if (speed > 1e-9)
    {
      const Vec3 axis = w * (-1 / speed);
      const double angle = speed * dt;
      up = up * std::cos(angle) + axis.cross(up) * std::sin(angle) + axis * (axis.dot(up) * (1 - std::cos(angle)));
      up = up * (1 / up.norm());
    }
    return true;
  }
  double forceVertical(Vec3 force) const { return force.dot(up) - forceOffset; }
};
