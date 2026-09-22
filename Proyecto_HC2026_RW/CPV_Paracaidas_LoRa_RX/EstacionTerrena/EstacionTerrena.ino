#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <esp_system.h>
#include "../RadioProtocol.h"

// ESP32 -> USB (115200) -> ui. Same SPI pins and RF settings as the CPV.
// No Serial writes in the radio task: a slow/disconnected PC cannot block RX.
struct UsbLine { char text[512]; };
QueueHandle_t outputQueue = nullptr, inputQueue = nullptr;
void report(const char *message)
{
  UsbLine line{};
  snprintf(line.text, sizeof(line.text), "%s", message);
  if (outputQueue) xQueueSend(outputQueue, &line, 0);
}
void reportCommand(const char *stage, const RadioProtocol::PendingCommand &pending, uint32_t result = 0)
{
  UsbLine line{};
  snprintf(line.text, sizeof(line.text), "UI_CMD,%s,%lu,%lu,%s,%lu", stage,
           (unsigned long)pending.packet.session, (unsigned long)pending.packet.sequence,
           RadioProtocol::commandName(pending.packet.command), (unsigned long)result);
  if (outputQueue) xQueueSend(outputQueue, &line, 0);
}
void reportTelemetry(const RadioProtocol::Telemetry &p, int rssi, float snr)
{
  UsbLine line{};
  const int length = snprintf(line.text, sizeof(line.text),
    "UI_TLM2,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%.4g,%.4g,%.4g,%.4g,%.4g,%.4g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%.2f,%lu",
    (unsigned long)p.boot, (unsigned long)p.sequence, (unsigned long)p.uptime, (unsigned long)p.flags,
    (unsigned long)p.source, (unsigned long)p.flightState, (unsigned long)p.epoch,
    p.ax, p.ay, p.az, p.gx, p.gy, p.gz, p.pressure, p.height, p.speed, p.acceleration,
    p.maxHeight, p.maxSpeed, p.maxAcceleration, rssi, snr, (unsigned long)p.magic);
  if (length > 0 && length < int(sizeof(line.text)) && outputQueue) xQueueSend(outputQueue, &line, 0);
}
void stationRadioTask(void *)
{
  SPI.begin(18, 19, 23, 5);
  LoRa.setPins(5, 14, 2);
  pinMode(2, INPUT);
  bool ready = false, transmitting = false, haveTelemetry = false;
  uint32_t retryAt = millis() - 1000, txStarted = 0, receivedAt = 0, sequence = 0;
  const uint32_t session = esp_random() | 1U;
  RadioProtocol::PendingCommand pending, transmitted, restartWatch;
  bool watchingRestart = false;
  uint32_t cpvBoot = 0, restartStarted = 0;
  for (;;)
  {
    uint32_t now = millis();
    Command command;
    // Queues are bounded, so malformed or continuous USB input cannot starve RX.
    for (unsigned n = 0; n < 4 && inputQueue && xQueueReceive(inputQueue, &command, 0) == pdTRUE; ++n)
    {
      const uint32_t nextSequence = sequence == UINT32_MAX ? 1 : sequence + 1;
      if (command == Command::Restart && !cpvBoot)
      {
        RadioProtocol::PendingCommand unavailable;
        unavailable.packet = {RadioProtocol::CommandMagic, session, nextSequence, 4, 0};
        sequence = nextSequence;
        reportCommand("SIN_DESTINO", unavailable);
        continue;
      }
      if (pending.begin(command, session, nextSequence, command == Command::Restart ? cpvBoot : 0))
      {
        sequence = nextSequence;
        if (command == Command::Release && watchingRestart)
        {
          reportCommand("CANCELADO", restartWatch);
          watchingRestart = false;
        }
        if (command == Command::Restart)
        {
          restartWatch = pending;
          watchingRestart = true;
          restartStarted = now;
        }
        reportCommand("PENDIENTE", pending);
      }
      else reportCommand("PENDIENTE", pending);
    }
    if (!ready && uint32_t(now - retryAt) >= 1000)
    {
      retryAt = now;
      if (LoRa.begin(433000000))
      {
        LoRa.setSpreadingFactor(7);
        LoRa.setSignalBandwidth(125E3);
        LoRa.setCodingRate4(5);
        LoRa.setSyncWord(0x12);
        LoRa.setPreambleLength(8);
        LoRa.enableCrc();
        LoRa.disableInvertIQ();
        LoRa.receive();
        ready = true;
        report("ESTACION LISTA: ARMAR / CALIBRAR / ACTIVAR / REINICIAR");
      }
      else report("ERROR LORA: reintento en 1 s");
    }
    if (ready && transmitting)
    {
      if (LoRa.beginPacket()) // Nonblocking completion probe (LoRa 0.8.0).
      {
        transmitting = false;
        LoRa.receive();
        reportCommand("TX", transmitted);
      }
      else if (uint32_t(now - txStarted) >= RadioProtocol::TxTimeoutMs)
      {
        LoRa.idle();
        LoRa.receive();
        transmitting = false;
        report("ERROR LORA: timeout TX; reintentando comando pendiente");
      }
    }
    if (ready && !transmitting && digitalRead(2) == HIGH)
    {
      const int length = LoRa.parsePacket();
      uint8_t bytes[sizeof(RadioProtocol::Telemetry)]{};
      unsigned count = 0;
      while (LoRa.available())
      {
        const uint8_t value = LoRa.read();
        if (count < sizeof(bytes)) bytes[count] = value;
        ++count;
      }
      const int rssi = LoRa.packetRssi();
      const float snr = LoRa.packetSnr();
      LoRa.receive();
      RadioProtocol::Telemetry telemetry;
      if (length == int(count) && RadioProtocol::decodeTelemetry(bytes, count, telemetry))
      {
        receivedAt = millis();
        haveTelemetry = true;
        cpvBoot = telemetry.boot;
        if (watchingRestart && cpvBoot != restartWatch.packet.targetBoot)
        {
          // Covers a lost ACK too. Stop all retries for this boot-bound reset.
          if (pending.packet.session == restartWatch.packet.session && pending.packet.sequence == restartWatch.packet.sequence)
            pending.active = false;
          reportCommand("REINICIADO", restartWatch);
          watchingRestart = false;
        }
        if (pending.acknowledge(telemetry))
        {
          reportCommand("ACK", pending, telemetry.ackResult);
          if (pending.packet.command == 4 && telemetry.ackResult == RadioProtocol::Ignored) watchingRestart = false;
        }
        reportTelemetry(telemetry, rssi, snr);
      }
    }
    now = millis();
    if (watchingRestart && uint32_t(now - restartStarted) >= 15000)
    {
      reportCommand("SIN_REINICIO", restartWatch);
      if (pending.packet.sequence == restartWatch.packet.sequence) pending.active = false;
      watchingRestart = false;
    }
    // After a downlink, allow the CPV 20 ms to restore RX. Otherwise transmit
    // immediately and retry with jitter: a missing downlink never blocks ACTIVAR.
    const bool turnaround = !haveTelemetry || uint32_t(now - receivedAt) >= 20;
    if (ready && !transmitting && turnaround && pending.due(now) && digitalRead(2) == LOW)
    {
      if (pending.expired())
      {
        reportCommand("SIN_CONFIRMACION", pending);
        pending.active = false;
      }
      else if (LoRa.beginPacket())
      {
        LoRa.write(reinterpret_cast<const uint8_t *>(&pending.packet), sizeof(pending.packet));
        LoRa.endPacket(true);
        pending.sent(now, esp_random());
        transmitted = pending;
        txStarted = now;
        transmitting = true;
      }
    }
    vTaskDelay(1);
  }
}
void setup()
{
  Serial.begin(115200);
  outputQueue = xQueueCreate(16, sizeof(UsbLine));
  inputQueue = xQueueCreate(4, sizeof(Command));
  if (!outputQueue || !inputQueue ||
      xTaskCreatePinnedToCore(stationRadioTask, "radio", 6144, nullptr, 2, nullptr, 0) != pdPASS)
    Serial.println("ERROR: no se pudo iniciar la estacion");
}
void loop()
{
  static char input[32]{};
  static unsigned length = 0;
  static bool overflow = false;
  for (unsigned n = 0; n < 64 && Serial.available(); ++n)
  {
    const char c = Serial.read();
    if (c == '\r' || c == '\n')
    {
      input[length] = 0;
      String text(input);
      text.trim();
      text.toUpperCase();
      const Command command = overflow ? Command::Unknown : decodeCommand(text.c_str(), text.length());
      if (command != Command::Unknown)
      {
        if (!inputQueue || xQueueSend(inputQueue, &command, 0) != pdTRUE) report("ERROR: cola de comandos llena");
      }
      else if (length || overflow) report("ERROR: utiliza ARMAR, CALIBRAR, ACTIVAR o REINICIAR");
      length = 0;
      overflow = false;
    }
    else if (length < sizeof(input) - 1) input[length++] = c;
    else overflow = true;
  }
  UsbLine line;
  if (outputQueue && xQueueReceive(outputQueue, &line, 0) == pdTRUE) Serial.println(line.text);
  delay(1);
}
