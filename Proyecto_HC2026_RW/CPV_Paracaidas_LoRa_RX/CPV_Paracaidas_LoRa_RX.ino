#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <atomic>
#include <esp_timer.h>
#include "RecoveryControl.h"
#include "Sensors.h"

// ESP32 dual core. LoRa + motor exclusively owned by radioTask (core 1).
// Sensors/calibration run on core 0; no sensor failure blocks radio reception.
constexpr int MOTOR_PIN_A = 25, MOTOR_PIN_B = 26;
constexpr int LORA_SCK = 18, LORA_MISO = 19, LORA_MOSI = 23, LORA_NSS = 5, LORA_RST = 14, LORA_DIO0 = 2;
std::atomic<bool> requestRelease{false}, airborne{false}, released{false}, closingMotor{false};
std::atomic<unsigned> calibrationEpoch{0};
QueueHandle_t logQueue = nullptr;
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
      if (motor.release(now))
        logMessage("PARACAIDAS: orden automatica; pulso de apertura 5 s");
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
        LoRa.disableCrc();
        LoRa.disableInvertIQ();
        LoRa.receive();
        radioReady = true;
        logMessage("LORA RX listo: 433 MHz, ARMAR / ACTIVAR, sin transmisiones");
      }
      else
        logMessage("ERROR LORA: no detectado; reintento en 1 s, automatico independiente");
    }
    // Poll RxDone, not parsePacket continuously: parsePacket's idle path
    // selects RX_SINGLE. Between completed packets stay in RX_CONTINUOUS.
    if (radioReady && digitalRead(LORA_DIO0) == HIGH)
    {
      const int length = LoRa.parsePacket();
      char packet[8]{};
      int count = 0;
      if (length > 0 && length <= 7)
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
      const Command cmd = count == length ? decodeCommand(packet, count) : Command::Unknown;
      now = millis();
      if (cmd == Command::Release)
      {
        if (motor.release(now, true))
          logMessage("PARACAIDAS: ACTIVAR por LoRa; pulso de apertura 5 s");
        else
          logMessage("ACTIVAR recibido: pulso de apertura ya en curso");
      }
      else if (cmd == Command::Arm)
      {
        if (motor.arm(now, airborne.load()))
        {
          closingMotor.store(true);
          calibrationEpoch.fetch_add(1);
          logMessage("ARMAR: cierre 1 s; despues recalibrar inmovil antes del vuelo");
        }
        else
          logMessage("ARMAR ignorado: vuelo iniciado, mecanismo ocupado o apertura ordenada");
      }
    }
    motor.tick(millis());
    released.store(motor.deployed);
    closingMotor.store(motor.direction == RecoveryControl::Closing);
    if (motor.direction != previous)
    {
      applyMotor(motor.direction);
      if (motor.direction == RecoveryControl::Off)
        logMessage("MOTOR apagado");
      previous = motor.direction;
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
  FlightLogic flight;
  VerticalProjection projection;
  bool imuPresent = false, baroPresent = false, calibrated = false;
  unsigned seenEpoch = calibrationEpoch.load(), n = 0;
  Vec3 sumForce{}, sumGyro{};
  double sumPressure = 0;
  double lastImuTime = 0, lastGoodForce = 0;
  uint32_t lastInit = millis() - 1000, lastLog = 0;
  TickType_t wake = xTaskGetTickCount();
  for (;;)
  {
    const unsigned epoch = calibrationEpoch.load();
    if (epoch != seenEpoch)
    {
      seenEpoch = epoch;
      calibrated = false;
      projection.valid = false;
      flight = FlightLogic();
      n = 0;
      sumForce = {};
      sumGyro = {};
      sumPressure = 0;
      logMessage("Calibracion reiniciada por ARMAR");
    }
    if (released.load())
    {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if ((!imuPresent || !baroPresent) && uint32_t(millis() - lastInit) >= 1000)
    {
      lastInit = millis();
      if (!imuPresent)
        imuPresent = beginImu();
      if (!baroPresent)
        baroPresent = bmp.begin();
      if (!imuPresent || !baroPresent)
        logMessage("Sensores pendientes: automatico no listo; ACTIVAR LoRa disponible");
      wake = xTaskGetTickCount();
    }
    double pressure = 0;
    const bool baroOk = baroPresent && bmp.read(pressure);
    ImuSample imu;
    const bool imuOk = imuPresent && readImu(imu);
    const double time = esp_timer_get_time() * 1e-6;
    // ARMAR may arrive while I2C is waiting. Discard that sensor pair rather
    // than applying the old flight state to a moving closing mechanism.
    if (calibrationEpoch.load() != seenEpoch || closingMotor.load())
    {
      vTaskDelayUntil(&wake, pdMS_TO_TICKS(20));
      continue;
    }
    if (!calibrated)
    {
      // Two seconds of approximately stationary, unsaturated readings.
      const bool stationary = imuOk && baroOk && !closingMotor.load() && !imu.accelClipped && !imu.gyroClipped &&
                              std::abs(imu.force.norm() - 9.81) < 1.0 && imu.gyro.norm() < .0873;
      if (!stationary)
      {
        n = 0;
        sumForce = {};
        sumGyro = {};
        sumPressure = 0;
      }
      else
      {
        sumForce = sumForce + imu.force;
        sumGyro = sumGyro + imu.gyro;
        sumPressure += pressure;
        ++n;
        if (n >= 100)
        {
          calibrated = projection.initialize(sumForce * (1.0 / n), sumGyro * (1.0 / n));
          if (calibrated)
          {
            flight = FlightLogic();
            flight.step(time, sumPressure / n, 9.81);
            lastImuTime = time;
            lastGoodForce = time;
            logMessage("AUTO LISTO: referencia y giro calibrados; mantener en reposo hasta lanzamiento");
          }
        }
      }
    }
    else
    {
      const double dt = time - lastImuTime;
      lastImuTime = time;
      if (!imuOk || imu.gyroClipped)
        projection.valid = false;
      else
        projection.update(imu.gyro, dt);
      bool forceValid = imuOk && !imu.accelClipped && projection.valid;
      if (!forceValid)
        lastGoodForce = time;
      // Allow 0.5 s of clean acceleration after saturation before prediction.
      forceValid = forceValid && (time - lastGoodForce >= .5);
      const double force = forceValid ? projection.forceVertical(imu.force) : 0;
      if (baroOk)
      {
        flight.step(time, pressure, force, forceValid);
        if (flight.state >= 1)
          airborne.store(true);
        if (flight.fired)
          requestRelease.store(true);
      }
      if (uint32_t(millis() - lastLog) >= 500)
      {
        lastLog = millis();
        LogLine line{};
        snprintf(line.text, sizeof(line.text), "USB h=%.2f Vr=%.2f az=%.2f eta=%.2f estado=%d baro=%d orient=%d", flight.h, flight.vr, flight.af, flight.eta, flight.state, baroOk, projection.valid);
        if (logQueue)
          xQueueSend(logQueue, &line, 0);
      }
    }
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
