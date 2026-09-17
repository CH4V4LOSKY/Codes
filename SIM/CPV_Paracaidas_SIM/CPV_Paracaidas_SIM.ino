#include <Arduino.h>
#include "FlightLogic.h"

// Bench variant of CPV_LoRa_Telemetria_Lib: USB replaces GY-87 inputs and
// LoRa transport. The PC supplies pressure and projected specific force;
// all velocity estimates and the parachute decision are computed here.
// No real sensors are read. Automatic release uses the DESARMAR direction.
constexpr int MOTOR_PIN_A = 25, MOTOR_PIN_B = 26;
constexpr uint32_t MOTOR_DURATION_MS = 5000;
bool motorRunning = false;
uint32_t motorStarted = 0;
uint32_t motorDuration = MOTOR_DURATION_MS;
const char *manualCommand = nullptr;
FlightLogic flight;
bool armed = false;
char line[200];
size_t used = 0;
bool overflow = false;

void safeOutputs()
{
  digitalWrite(MOTOR_PIN_A, LOW);
  digitalWrite(MOTOR_PIN_B, LOW);
  motorRunning = false;
  manualCommand = nullptr;
}
void updateMotor()
{
  // Wall-clock timing continues even when USB stops sending samples.
  if (motorRunning && (uint32_t)(millis() - motorStarted) >= motorDuration)
  {
    const char *completed = manualCommand;
    safeOutputs();
    if (completed) { Serial.print("DONE,"); Serial.println(completed); }
  }
}
void processLine()
{
  if (!strcmp(line, "ARMAR") || !strcmp(line, "DESARMAR"))
  {
    if (motorRunning || armed) { Serial.println("ERR,BUSY"); return; }
    const bool closing = !strcmp(line, "ARMAR");
    safeOutputs();
    manualCommand = closing ? "ARMAR" : "DESARMAR";
    motorDuration = 1000;
    motorStarted = millis();
    digitalWrite(closing ? MOTOR_PIN_B : MOTOR_PIN_A, HIGH);
    motorRunning = true;
    Serial.print("OK,"); Serial.println(manualCommand);
    return;
  }
  if (!strcmp(line, "HELLO"))
  {
    Serial.println("READY,CPV_SIM,4,MOTOR");
    return;
  }
  if (!strcmp(line, "STOP"))
  {
    armed = false;
    safeOutputs();
    Serial.println("STOPPED");
    return;
  }
  if (!strcmp(line, "RESET"))
  {
    flight = FlightLogic();
    armed = false;
    safeOutputs();
    Serial.println("RESET_OK");
    return;
  }
  if (!strcmp(line, "ARM"))
  {
    if (motorRunning) { Serial.println("ERR,BUSY"); return; }
    if (flight.t >= 0)
    {
      Serial.println("ERR,RESET_REQUIRED");
      return;
    }
    armed = true;
    Serial.println("ARMED");
    return;
  }
  if (!strncmp(line, "CFG,", 4))
  {
    double tau;
    char extra;
    if (armed || flight.t >= 0 || sscanf(line, "CFG,%lf%c", &tau, &extra) != 1 || !isfinite(tau) || tau < 0.02 || tau > 5)
    {
      Serial.println("ERR,CONFIG");
      return;
    }
    flight.tau = tau;
    Serial.println("CONFIG_OK");
    return;
  }
  unsigned long seq;
  double t, p, f;
  char extra;
  if (sscanf(line, "S,%lu,%lf,%lf,%lf%c", &seq, &t, &p, &f, &extra) != 4)
  {
    Serial.println("ERR,FORMAT");
    return;
  }
  if (!armed)
  {
    Serial.println("ERR,NOT_ARMED");
    return;
  }
  const bool alreadyFired = flight.fired;
  if (!flight.step(t, p, f))
  {
    Serial.println("ERR,SAMPLE");
    return;
  }
  if (flight.fired && !alreadyFired)
  {
    digitalWrite(MOTOR_PIN_B, LOW);
    motorStarted = millis();
    motorDuration = MOTOR_DURATION_MS;
    digitalWrite(MOTOR_PIN_A, HIGH);
    motorRunning = true;
  }
  Serial.printf("R,%lu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d\n",
                seq, flight.t, flight.h, flight.vr, flight.vb, flight.va, flight.af, flight.eta, flight.state, flight.fired ? 1 : 0);
}
void setup()
{
  pinMode(MOTOR_PIN_A, OUTPUT);
  pinMode(MOTOR_PIN_B, OUTPUT);
  safeOutputs();
  Serial.begin(115200);
  Serial.println("READY,CPV_SIM,4,MOTOR");
}
void loop()
{
  updateMotor();
  while (Serial.available())
  {
    updateMotor();
    char c = Serial.read();
    if (c == '\r')
      continue;
    if (c == '\n')
    {
      if (overflow)
        Serial.println("ERR,LINE_TOO_LONG");
      else
      {
        line[used] = '\0';
        processLine();
      }
      used = 0;
      overflow = false;
    }
    else if (used < sizeof(line) - 1 && !overflow)
      line[used++] = c;
    else
      overflow = true;
  }
}
