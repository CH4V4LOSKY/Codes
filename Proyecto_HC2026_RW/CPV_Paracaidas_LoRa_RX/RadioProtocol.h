#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <cmath>
#include "RecoveryControl.h"

// Version 2 wire format: fixed-width, little-endian ESP32 fields. Both radios
// use this header; the ground station converts telemetry to text for the UI.
namespace RadioProtocol
{
constexpr uint32_t TelemetryMagic = 0x32565043; // CPV2
constexpr uint32_t CommandMagic = 0x32444d43;   // CMD2
constexpr uint32_t PeriodMs = 500, TxTimeoutMs = 500, StaleMs = 300;
enum Flag : uint32_t
{
  ImuOk = 1, BaroOk = 2, Calibrated = 4, OrientationOk = 8,
  Deployed = 16, Airborne = 32, MotorOpening = 64, MotorClosing = 128,
  AccelerationOk = 256, Calibrating = 512, Armed = 1024,
  ReferenceReady = 2048, CalibrationFailed = 4096, RestartPending = 8192
};
enum Result : uint32_t { None = 0, Accepted = 1, Ignored = 2 };
enum Source : uint32_t { NotReleased = 0, Automatic = 1, Manual = 2 };
struct CommandPacket
{
  uint32_t magic = CommandMagic, session = 0, sequence = 0, command = 0;
  uint32_t targetBoot = 0; // Required for REINICIAR; prevents replay after reboot.
};
struct Telemetry
{
  uint32_t magic = TelemetryMagic, boot = 0, sequence = 0, uptime = 0, flags = 0, epoch = 0;
  uint32_t ackSession = 0, ackSequence = 0, ackCommand = 0, ackResult = None;
  uint32_t source = NotReleased, flightState = 0;
  float ax = NAN, ay = NAN, az = NAN, gx = NAN, gy = NAN, gz = NAN;
  float pressure = NAN, height = NAN, speed = NAN, acceleration = NAN;
  float maxHeight = NAN, maxSpeed = NAN, maxAcceleration = NAN;
};
static_assert(sizeof(CommandPacket) == 20, "Command wire layout changed");
static_assert(sizeof(Telemetry) == 100, "Telemetry wire layout changed");
inline uint32_t commandCode(Command command)
{
  return command == Command::Arm ? 1 : command == Command::Release ? 2 : command == Command::Calibrate ? 3 : command == Command::Restart ? 4 : 0;
}
inline const char *commandName(uint32_t code)
{
  return code == 1 ? "ARMAR" : code == 2 ? "ACTIVAR" : code == 3 ? "CALIBRAR" : code == 4 ? "REINICIAR" : "DESCONOCIDO";
}
inline Command decodePacket(const void *data, size_t size, CommandPacket &packet)
{
  if (size != sizeof(packet) && size != 16) return Command::Unknown;
  packet = CommandPacket();
  memcpy(&packet, data, size); // Accept the old 16-byte format for non-reset commands.
  if (packet.magic != CommandMagic || !packet.session || !packet.sequence) return Command::Unknown;
  return packet.command == 1 ? Command::Arm : packet.command == 2 ? Command::Release :
         packet.command == 3 ? Command::Calibrate : packet.command == 4 && packet.targetBoot ? Command::Restart : Command::Unknown;
}
inline bool decodeTelemetry(const void *data, size_t size, Telemetry &packet)
{
  if (size != sizeof(packet)) return false;
  memcpy(&packet, data, sizeof(packet));
  return packet.magic == TelemetryMagic && packet.source <= Manual && packet.ackResult <= Ignored;
}
inline void maximum(float &peak, float value)
{
  if (std::isfinite(value) && (!std::isfinite(peak) || value > peak)) peak = value;
}
inline void clearCurrent(Telemetry &packet)
{
  packet.flags &= ~(ImuOk | BaroOk | OrientationOk | AccelerationOk);
  packet.ax = packet.ay = packet.az = packet.gx = packet.gy = packet.gz = NAN;
  packet.pressure = packet.height = packet.speed = packet.acceleration = NAN;
}
// Cache results for retries, including rejected commands. Repeated packets
// must not restart a completed motor pulse or repeat calibration.
struct CommandHistory
{
  struct Entry { uint32_t session = 0, sequence = 0, command = 0, result = None; };
  Entry entries[16]{};
  unsigned next = 0;
  uint32_t find(const CommandPacket &p) const
  {
    for (const auto &e : entries)
      if (e.session == p.session && e.sequence == p.sequence)
        return e.command == p.command ? e.result : Ignored;
    return None;
  }
  void remember(const CommandPacket &p, uint32_t result)
  {
    entries[next] = {p.session, p.sequence, p.command, result};
    next = (next + 1) % 16;
  }
};
// One pending intent. Only ACTIVAR preempts preparation commands. Repeated clicks while
// waiting retain the same identity. A later deliberate command gets a new ID.
struct PendingCommand
{
  CommandPacket packet;
  bool active = false;
  uint32_t attempts = 0, sentAt = 0, interval = 0;
  bool begin(Command command, uint32_t session, uint32_t sequence, uint32_t targetBoot = 0)
  {
    if (command == Command::Unknown || (command == Command::Restart && !targetBoot)) return false;
    if (active && (packet.command == commandCode(command) || command != Command::Release)) return false;
    packet = {CommandMagic, session, sequence, commandCode(command), targetBoot};
    active = true;
    attempts = 0;
    return true;
  }
  bool due(uint32_t now) const { return active && (!attempts || uint32_t(now - sentAt) >= interval); }
  bool expired() const { return attempts >= 8; }
  void sent(uint32_t now, uint32_t jitter)
  {
    ++attempts;
    sentAt = now;
    interval = 700 + jitter % 201;
  }
  bool acknowledge(const Telemetry &t)
  {
    if (!active || t.ackSession != packet.session || t.ackSequence != packet.sequence ||
        t.ackCommand != packet.command || t.ackResult == None) return false;
    active = false;
    return true;
  }
};
}
