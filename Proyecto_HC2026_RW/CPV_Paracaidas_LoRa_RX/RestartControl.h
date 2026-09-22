#pragma once
#include <stdint.h>

// A reboot is announced before executing. Motor pulses retain their full
// duration, and a new release request always takes priority over resetting.
struct RestartControl
{
  bool pending = false, acknowledged = false;
  uint32_t requestedAt = 0;
  void request(uint32_t now)
  {
    if (pending) return;
    pending = true;
    acknowledged = false;
    requestedAt = now;
  }
  void cancel() { pending = false; acknowledged = false; }
  bool due(uint32_t now, bool motorOff, bool transmitting) const
  {
    return pending && acknowledged && motorOff && !transmitting && uint32_t(now - requestedAt) >= 750;
  }
};
