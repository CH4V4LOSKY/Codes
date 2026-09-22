#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <atomic>
#include <esp_timer.h>
#include <esp_system.h>
#include "RecoveryControl.h"
#include "Sensors.h"
#include "RadioProtocol.h"
#include "FlightPreparation.h"
#include "RestartControl.h"

// ESP32 dual core. LoRa + motor exclusively owned by radioTask (core 1).
// Sensors/calibration run on core 0; no sensor failure blocks radio reception.
constexpr int MOTOR_PIN_A = 25, MOTOR_PIN_B = 26;
constexpr int LORA_SCK = 18, LORA_MISO = 19, LORA_MOSI = 23, LORA_NSS = 5, LORA_RST = 14, LORA_DIO0 = 2;
std::atomic<bool> requestRelease{false}, airborne{false}, released{false}, closingMotor{false};
// A request stays pending until the sensor task completes or cancels it.
std::atomic<unsigned> calibrationRequested{0}, calibrationHandled{0};
QueueHandle_t logQueue = nullptr;
QueueHandle_t telemetryQueue = nullptr;
struct LogLine
{
  char text[160];
};

void logMessage(const char *message)
{
  if (!logQueue)
    return;
  LogLine line{};
  snprintf(line.text, sizeof(line.text), "%s", message);
  xQueueSend(logQueue, &line, 0); // Drop a log rather than delay radio/motor.
}

void applyMotor(RecoveryControl::Direction direction)
{
  digitalWrite(MOTOR_PIN_A, LOW);
  digitalWrite(MOTOR_PIN_B, LOW);
  if (direction == RecoveryControl::Closing)
    digitalWrite(MOTOR_PIN_B, HIGH);
  if (direction == RecoveryControl::Opening)
    digitalWrite(MOTOR_PIN_A, HIGH);
}

void radioTask(void *)
{
  RecoveryControl motor;
  RestartControl restart;
  bool txHasRestartAck = false;
  RadioProtocol::Telemetry telemetry;
  RadioProtocol::CommandHistory commands;
  RadioProtocol::CommandPacket acknowledgment;
  uint32_t ackResult = RadioProtocol::None, source = RadioProtocol::NotReleased;
  const uint32_t boot = esp_random() | 1U;
  uint32_t sequence = 0, lastTx = millis() - RadioProtocol::PeriodMs, txStarted = 0;
  bool transmitting = false, armed = false;
  RecoveryControl::Direction previous = RecoveryControl::Off;
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  pinMode(LORA_DIO0, INPUT);
  bool radioReady = false;
  uint32_t retryAt = millis() - 1000;
  for (;;)
  {
    uint32_t now = millis();
    motor.tick(now);
    if (requestRelease.exchange(false))
    {
      restart.cancel();
      if (motor.release(now))
      {
        if (source == RadioProtocol::NotReleased) source = RadioProtocol::Automatic;
        logMessage("PARACAIDAS: orden automatica; pulso de apertura 5 s");
      }
    }
    if (!radioReady && motor.direction == RecoveryControl::Off && !motor.reversing && uint32_t(now - retryAt) >= 1000)
    {
      retryAt = now;
      if (LoRa.begin(433000000))
      {
        // Exact settings from the existing ground station sketch.
        LoRa.setSpreadingFactor(7);
        LoRa.setSignalBandwidth(125E3);
        LoRa.setCodingRate4(5);
        LoRa.setSyncWord(0x12);
        LoRa.setPreambleLength(8);
        LoRa.enableCrc();
        LoRa.disableInvertIQ();
        LoRa.receive();
        radioReady = true;
        logMessage("LORA listo: ARMAR / CALIBRAR / ACTIVAR / REINICIAR; telemetria 2 Hz");
      }
      else
        logMessage("ERROR LORA: no detectado; reintento en 1 s, automatico independiente");
    }
    // Poll RxDone, not parsePacket continuously: parsePacket's idle path
    // selects RX_SINGLE. Between completed packets stay in RX_CONTINUOUS.
    if (radioReady && transmitting)
    {
      // LoRa 0.8.0 keeps isTransmitting private. beginPacket returns 0 while
      // TX is active; on completion it clears TxDone and enters standby.
      if (LoRa.beginPacket())
      {
        if (txHasRestartAck && restart.pending) restart.acknowledged = true;
        txHasRestartAck = false;
        transmitting = false;
        LoRa.receive();
      }
      else if (uint32_t(now - txStarted) >= RadioProtocol::TxTimeoutMs)
      {
        LoRa.idle();
        LoRa.receive();
        transmitting = false;
        txHasRestartAck = false;
        logMessage("LORA: timeout TX; recepcion restaurada");
      }
    }
    if (radioReady && !transmitting && digitalRead(LORA_DIO0) == HIGH)
    {
      const int length = LoRa.parsePacket();
      char packet[sizeof(RadioProtocol::CommandPacket)]{};
      int count = 0;
      if (length > 0 && length <= int(sizeof(packet)))
      {
        while (LoRa.available() && count < length)
          packet[count++] = char(LoRa.read());
      }
      else
      {
        while (LoRa.available())
          LoRa.read();
      }
      // Restore continuous reception before executing the command.
      LoRa.receive();
      RadioProtocol::CommandPacket framed;
      Command cmd = count == length ? RadioProtocol::decodePacket(packet, count, framed) : Command::Unknown;
      const bool identified = cmd != Command::Unknown;
      if (!identified && count == length) cmd = decodeCommand(packet, count);
      const uint32_t cached = identified ? commands.find(framed) : RadioProtocol::None;
      uint32_t result = RadioProtocol::Ignored;
      now = millis();
      if (cached != RadioProtocol::None)
        result = cached;
      else if (cmd == Command::Release)
      {
        restart.cancel();
        if (motor.release(now, true))
        {
          if (source == RadioProtocol::NotReleased) source = RadioProtocol::Manual;
          result = RadioProtocol::Accepted;
          logMessage("PARACAIDAS: ACTIVAR por LoRa; pulso de apertura 5 s");
        }
        else
        {
          // Opening already in progress satisfies this intent, without extending it.
          result = RadioProtocol::Accepted;
          logMessage("ACTIVAR recibido: pulso de apertura ya en curso");
        }
      }
      else if (cmd == Command::Arm)
      {
        if (!restart.pending && calibrationRequested.load() == calibrationHandled.load() && motor.arm(now, airborne.load()))
        {
          result = RadioProtocol::Accepted;
          closingMotor.store(true);
          armed = false;
          logMessage("ARMAR: solo cierre 1 s; CALIBRAR se solicita por separado");
        }
        else
          logMessage("ARMAR ignorado: vuelo, calibracion o mecanismo ocupado");
      }
      else if (cmd == Command::Calibrate)
      {
        if (!restart.pending && !airborne.load() && !motor.deployed && motor.direction == RecoveryControl::Off && !motor.reversing &&
            calibrationRequested.load() == calibrationHandled.load())
        {
          calibrationRequested.fetch_add(1);
          result = RadioProtocol::Accepted;
          logMessage("CALIBRAR solicitado: mantener inmovil; deteccion de vuelo sigue activa");
        }
        else logMessage("CALIBRAR ignorado: vuelo, apertura o preparacion en curso");
      }
      else if (cmd == Command::Restart)
      {
        // Never accept a bare/unbound reset, including a delayed retry from
        // before the last reboot. No flash write is needed for deduplication.
        if (identified && framed.targetBoot == boot)
        {
          restart.request(now);
          result = RadioProtocol::Accepted;
          logMessage("REINICIAR aceptado: confirmar por LoRa y esperar motor apagado");
        }
        else logMessage("REINICIAR ignorado: identificador de arranque no coincide");
      }
      if (identified)
      {
        if (cached == RadioProtocol::None) commands.remember(framed, result);
        acknowledgment = framed;
        ackResult = result;
      }
    }
    motor.tick(millis());
    if (previous == RecoveryControl::Closing && motor.direction == RecoveryControl::Off && !motor.deployed)
      armed = true;
    released.store(motor.deployed);
    if (motor.deployed) armed = false;
    closingMotor.store(motor.direction == RecoveryControl::Closing);
    if (motor.direction != previous)
    {
      applyMotor(motor.direction);
      if (motor.direction == RecoveryControl::Off)
        logMessage("MOTOR apagado");
      previous = motor.direction;
    }
    if (restart.due(millis(), motor.direction == RecoveryControl::Off && !motor.reversing, transmitting))
    {
      applyMotor(RecoveryControl::Off);
      esp_restart(); // setup() restores the initial unarmed, uncalibrated state.
    }
    // Copy the latest sample without waiting for I2C. Startup, calibration,
    // sensor outages, opening and descent all retain the same radio schedule.
    if (telemetryQueue) xQueuePeek(telemetryQueue, &telemetry, 0);
    now = millis();
    if (radioReady && !transmitting && uint32_t(now - lastTx) >= RadioProtocol::PeriodMs && digitalRead(LORA_DIO0) == LOW)
    {
      RadioProtocol::Telemetry packet = telemetry;
      if (uint32_t(now - packet.uptime) > RadioProtocol::StaleMs) RadioProtocol::clearCurrent(packet);
      packet.flags &= ~RadioProtocol::Calibrating;
      if (calibrationRequested.load() != calibrationHandled.load())
      {
        packet.flags |= RadioProtocol::Calibrating;
        packet.flags &= ~RadioProtocol::CalibrationFailed;
      }
      packet.boot = boot;
      packet.sequence = sequence++;
      packet.uptime = now;
      packet.flags |= (motor.deployed ? RadioProtocol::Deployed : 0) |
                      (airborne.load() ? RadioProtocol::Airborne : 0) |
                      (motor.direction == RecoveryControl::Opening ? RadioProtocol::MotorOpening : 0) |
                      (motor.direction == RecoveryControl::Closing ? RadioProtocol::MotorClosing : 0) |
                      (armed ? RadioProtocol::Armed : 0) |
                      (restart.pending ? RadioProtocol::RestartPending : 0);
      packet.source = source;
      packet.ackSession = acknowledgment.session;
      packet.ackSequence = acknowledgment.sequence;
      packet.ackCommand = acknowledgment.command;
      packet.ackResult = ackResult;
      if (LoRa.beginPacket())
      {
        LoRa.write(reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
        LoRa.endPacket(true); // Never wait for TxDone in the motor task.
        txHasRestartAck = restart.pending && packet.ackCommand == 4 && packet.ackResult == RadioProtocol::Accepted;
        transmitting = true;
        txStarted = lastTx = now;
      }
    }
    vTaskDelay(1);
  }
}

void sensorTask(void *)
{
  Wire.begin(21, 22);
  Wire.setClock(400000);
  Wire.setTimeOut(10);
  Bmp180 bmp;
  FlightMonitor monitor;
  RadioProtocol::Telemetry snapshot;
  bool imuPresent = false, baroPresent = false;
  unsigned seenRequest = 0;
  uint32_t lastInit = millis() - 1000, lastLog = 0;
  TickType_t wake = xTaskGetTickCount();
  for (;;)
  {
    if ((!imuPresent || !baroPresent) && uint32_t(millis() - lastInit) >= 1000)
    {
      lastInit = millis();
      if (!imuPresent) imuPresent = beginImu();
      if (!baroPresent) baroPresent = bmp.begin();
      if (!imuPresent || !baroPresent)
        logMessage("Sensores pendientes; deteccion barometrica si hay presion valida; ACTIVAR disponible");
      wake = xTaskGetTickCount();
    }
    double pressure = 0;
    const bool baroOk = baroPresent && bmp.read(pressure);
    ImuSample imu;
    const bool imuOk = imuPresent && readImu(imu);
    const double time = esp_timer_get_time() * 1e-6;
    const unsigned requested = calibrationRequested.load();
    if (requested != seenRequest)
    {
      seenRequest = requested;
      if (!monitor.startCalibration(time, closingMotor.load(), released.load()))
      {
        monitor.calibration.failed = true;
        calibrationHandled.store(seenRequest);
        logMessage("CALIBRAR cancelado: las condiciones cambiaron antes de comenzar");
      }
      else logMessage("CALIBRANDO por orden manual: 100 muestras filtradas y estables (~2 s); limite 15 s");
    }

    const bool wasCalibrating = monitor.calibration.active;
    const unsigned previousEpoch = monitor.epoch;
    // No preparation command, motor state or unfinished calibration gates this
    // update. Launch detection always runs before a candidate is committed.
    monitor.update(time, pressure, imu, imuOk, baroOk, closingMotor.load(), released.load());
    if (monitor.flight.state >= 1) airborne.store(true);
    if (monitor.flight.fired && !released.load()) requestRelease.store(true);
    if (monitor.epoch != previousEpoch)
    {
      snapshot = RadioProtocol::Telemetry();
      logMessage("CALIBRACION COMPLETA: referencia y giro actualizados por orden manual");
    }
    if (wasCalibrating && !monitor.calibration.active)
    {
      calibrationHandled.store(seenRequest);
      if (monitor.calibration.failed)
        logMessage("CALIBRACION CANCELADA: vuelo/apertura detectados o sin reposo valido en 15 s");
    }

    const FlightLogic &flight = monitor.flight;
    if (uint32_t(millis() - lastLog) >= 500)
    {
      lastLog = millis();
      LogLine line{};
      snprintf(line.text, sizeof(line.text), "USB h=%.2f Vr=%.2f az=%.2f eta=%.2f estado=%d baro=%d orient=%d",
               flight.h, flight.vr, flight.af, flight.eta, flight.state, baroOk, monitor.projection.valid);
      if (logQueue) xQueueSend(logQueue, &line, 0);
    }
    snapshot.uptime = millis();
    snapshot.epoch = monitor.epoch;
    snapshot.flags = (imuOk ? RadioProtocol::ImuOk : 0) | (baroOk ? RadioProtocol::BaroOk : 0) |
                     (monitor.calibrated ? RadioProtocol::Calibrated : 0) |
                     (monitor.projection.valid ? RadioProtocol::OrientationOk : 0) |
                     (flight.t >= 0 ? RadioProtocol::ReferenceReady : 0) |
                     (monitor.calibration.active ? RadioProtocol::Calibrating : 0) |
                     (monitor.calibration.failed ? RadioProtocol::CalibrationFailed : 0);
    snapshot.ax = imuOk ? float(imu.force.x / 9.80665) : NAN;
    snapshot.ay = imuOk ? float(imu.force.y / 9.80665) : NAN;
    snapshot.az = imuOk ? float(imu.force.z / 9.80665) : NAN;
    snapshot.gx = imuOk ? float(imu.gyro.x * 180 / 3.141592653589793) : NAN;
    snapshot.gy = imuOk ? float(imu.gyro.y * 180 / 3.141592653589793) : NAN;
    snapshot.gz = imuOk ? float(imu.gyro.z * 180 / 3.141592653589793) : NAN;
    snapshot.pressure = baroOk ? float(pressure / 100) : NAN;
    snapshot.height = flight.t >= 0 && baroOk ? float(flight.h) : NAN;
    snapshot.speed = flight.t >= 0 && baroOk ? float(flight.vr) : NAN;
    if (monitor.forceValid && baroOk) snapshot.flags |= RadioProtocol::AccelerationOk;
    snapshot.acceleration = monitor.forceValid && baroOk ? float(flight.af) : NAN;
    snapshot.flightState = flight.state;
    RadioProtocol::maximum(snapshot.maxHeight, snapshot.height);
    RadioProtocol::maximum(snapshot.maxSpeed, std::abs(snapshot.speed));
    RadioProtocol::maximum(snapshot.maxAcceleration, std::abs(snapshot.acceleration));
    if (telemetryQueue) xQueueOverwrite(telemetryQueue, &snapshot);
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(20));
  }
}

void setup()
{
  pinMode(MOTOR_PIN_A, OUTPUT);
  pinMode(MOTOR_PIN_B, OUTPUT);
  applyMotor(RecoveryControl::Off);
  Serial.begin(115200);
  logQueue = xQueueCreate(16, sizeof(LogLine));
  telemetryQueue = xQueueCreate(1, sizeof(RadioProtocol::Telemetry));
  // No startup motor test. Radio starts before sensor calibration.
  const BaseType_t rx = xTaskCreatePinnedToCore(radioTask, "radio_motor", 4096, nullptr, 3, nullptr, 1);
  const BaseType_t sensors = xTaskCreatePinnedToCore(sensorTask, "sensores", 6144, nullptr, 1, nullptr, 0);
  if (rx != pdPASS)
    Serial.println("ERROR: no se pudo iniciar la tarea LoRa/motor");
  if (sensors != pdPASS)
    Serial.println("ERROR: no se pudo iniciar sensores; LoRa sigue independiente");
}
void loop()
{
  // USB logs cannot block the high-priority radio task.
  LogLine line;
  if (logQueue && xQueueReceive(logQueue, &line, 0) == pdTRUE)
    Serial.println(line.text);
  delay(1);
}
