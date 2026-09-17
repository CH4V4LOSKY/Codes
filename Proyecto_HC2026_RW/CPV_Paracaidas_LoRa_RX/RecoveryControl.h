#pragma once
#include <stdint.h>
#include <string.h>

enum class Command
{
  Unknown,
  Arm,
  Release
};
inline Command decodeCommand(const char *data, unsigned length)
{
  // Exact payloads emitted by EstacionTerrena_LoRa_Lib. No CMD: wrapper.
  if (length == 5 && memcmp(data, "ARMAR", 5) == 0)
    return Command::Arm;
  if (length == 7 && memcmp(data, "ACTIVAR", 7) == 0)
    return Command::Release;
  return Command::Unknown;
}

struct RecoveryControl
{
  enum Direction
  {
    Off,
    Closing,
    Opening
  };
  Direction direction = Off;
  bool deployed = false, reversing = false;
  uint32_t started = 0;
  bool arm(uint32_t now, bool airborne)
  {
    if (airborne || deployed || direction != Off || reversing)
      return false;
    direction = Closing;
    started = now;
    return true;
  }
  bool release(uint32_t now, bool manualRetry = false)
  {
    // An explicit new ground command may retry after a completed pulse.
    // Packets received during opening never extend or restart that pulse.
    if (deployed && (!manualRetry || direction != Off || reversing))
      return false;
    deployed = true;
    reversing = direction == Closing;
    direction = reversing ? Off : Opening;
    started = now;
    return true;
  }
  void tick(uint32_t now)
  {
    if (reversing && uint32_t(now - started) >= 20)
    {
      reversing = false;
      direction = Opening;
      started = now;
    }
    if ((direction == Closing && uint32_t(now - started) >= 1000) ||
        (direction == Opening && uint32_t(now - started) >= 5000))
      direction = Off;
  }
};
