#pragma once
#include <math.h>
// main.pdf equations 2, 6, 7, 25, 32. Thresholds are bench-test choices.
struct FlightLogic
{
  double tau = 0.4, accelTau = 0.1, launchHeight = 3, launchSpeed = 3;
  double descentSpeed = 0.5, drop = 0.5, hold = 0.15;
  double lead = 2.0; // User-requested order at <=2 s to predicted apogee.
  double t = -1, p0 = 0, h = 0, vr = 0, vb = 0, va = 0, az = 0, af = 0, eta = -1, peak = 0, since = -1;
  int state = 0;
  bool fired = false;
  bool step(double time, double pressure, double force)
  {
    if (!isfinite(time) || !isfinite(pressure) || !isfinite(force) || time < 0 || pressure <= 0)
      return false;
    if (t >= 0 && (time <= t || time - t > 0.25))
      return false;
    double a = force - 9.81;
    if (t < 0)
    {
      p0 = pressure;
      t = time;
      az = af = a;
      return true;
    }
    double dt = time - t, height = (287.0 * 293.15 / 9.81) * log(p0 / pressure);
    vb = (height - h) / dt;
    va = vr + (az + a) * 0.5 * dt;
    double alpha = tau / (tau + dt);
    vr = alpha * va + (1 - alpha) * vb;
    af += dt / (accelTau + dt) * (a - af);
    az = a;
    h = height;
    t = time;
    if (h > peak)
      peak = h;
    eta = vr > 0 && af < -0.1 ? -vr / af : -1;
    if (state == 0 && h >= launchHeight && vr >= launchSpeed)
      state = 1;
    if (state == 1)
    {
      bool descent = vr < -descentSpeed && peak - h >= drop;
      if (!descent)
        since = -1;
      else if (since < 0)
        since = t;
      bool anticipate = eta > 0 && eta <= lead;
      if (anticipate || (since >= 0 && t - since + 1e-8 >= hold))
      {
        state = 2;
        fired = true;
      }
    }
    return true;
  }
};
