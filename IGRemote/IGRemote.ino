// ============================================================
// IGRemote - Arduino Nano, receptor LoRa para prueba segura
//
// Recibe el comando hexadecimal del master, inicia una cuenta
// regresiva segura y reporta estados al master por LoRa.
// Usa un LED externo en D5 para indicar la ejecucion.
// ============================================================

#include <SPI.h>
#include <LoRa.h>

// ---------- Pines LoRa para Arduino Nano ----------
// SPI hardware del Nano: SCK=D13, MISO=D12, MOSI=D11.
#define LORA_SS 10
#define LORA_RST 9
#define LORA_DIO0 2

#define LED_EXEC_PIN 5

// ---------- Configuracion LoRa ----------
const long LORA_FREQUENCY = 433000000L;
const uint8_t LORA_SYNC_WORD = 0x34;

// ---------- Protocolo binario ----------
const uint16_t PACKET_MAGIC = 0xC0DE;
const uint32_t MASTER_IG_ID = 0xA71C5E2D;
const uint32_t REMOTE_IG_ID = 0x9E771026;
const uint32_t COMMAND_ACTIVATE = 0x51B7E20C;
const uint32_t STATUS_COUNTDOWN_STARTED = 0xC0D15A7A;
const uint32_t STATUS_SAFE_ACTION_STARTED = 0xE5EC0001;
const uint32_t STATUS_SAFE_ACTION_DONE = 0xD04E0001;
const uint32_t STATUS_REMOTE_BUSY = 0xB105EADD;

const uint8_t PACKET_SIZE =
    sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t);

// ---------- Tiempos ----------
const unsigned long START_LOCK_MS = 5000;
const unsigned long COUNTDOWN_MS = 5000;
const unsigned long COUNTDOWN_PRINT_INTERVAL_MS = 1000;
const unsigned long SAFE_OUTPUT_MS = 500;
const unsigned long COOLDOWN_MS = 5000;

enum SystemState {
  START_LOCKED,
  READY,
  COUNTDOWN,
  SIMULATING,
  COOLDOWN
};

SystemState state = START_LOCKED;
unsigned long stateStartedAt = 0;
unsigned long lastCountdownPrintAt = 0;
uint32_t activeCommandSequence = 0;

void updateState(unsigned long now);
void receiveLoRa(unsigned long now);
void beginCountdown(uint32_t sequence, unsigned long now);
void printCountdown(unsigned long now);
void beginSafeSimulation(unsigned long now);
void finishSafeSimulation(unsigned long now);
void sendStatus(uint32_t sequence, uint32_t statusCommand);
void setState(SystemState nextState, unsigned long now);
bool elapsed(unsigned long now, unsigned long since, unsigned long intervalMs);
void discardPacket();
uint16_t readU16();
uint32_t readU32();
void writeU16(uint16_t value);
void writeU32(uint32_t value);
uint16_t checksumPacket(uint16_t magic, uint32_t deviceId, uint32_t sequence, uint32_t command);

// ============================================================
void setup() {
  Serial.begin(9600);

  pinMode(LED_EXEC_PIN, OUTPUT);
  digitalWrite(LED_EXEC_PIN, LOW);

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println(F("ERROR: no se detecta el modulo LoRa"));
    while (true) {
      delay(1000);
    }
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.enableCrc();
  LoRa.receive();

  stateStartedAt = millis();
  Serial.println(F("IGRemote listo"));
  Serial.println(F("Bloqueo inicial de 5 s"));
}

// ============================================================
void loop() {
  unsigned long now = millis();

  updateState(now);
  receiveLoRa(now);
}

void updateState(unsigned long now) {
  if (state == START_LOCKED && elapsed(now, stateStartedAt, START_LOCK_MS)) {
    setState(READY, now);
    Serial.println(F("IGRemote listo para recibir comando"));
  }

  if (state == COUNTDOWN) {
    printCountdown(now);

    if (elapsed(now, stateStartedAt, COUNTDOWN_MS)) {
      beginSafeSimulation(now);
    }
  }

  if (state == SIMULATING && elapsed(now, stateStartedAt, SAFE_OUTPUT_MS)) {
    finishSafeSimulation(now);
  }

  if (state == COOLDOWN && elapsed(now, stateStartedAt, COOLDOWN_MS)) {
    setState(READY, now);
    Serial.println(F("Enfriamiento terminado; listo para otro comando"));
  }
}

void receiveLoRa(unsigned long now) {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) {
    return;
  }

  if (packetSize != PACKET_SIZE) {
    discardPacket();
    Serial.println(F("Paquete descartado: longitud invalida"));
    return;
  }

  uint16_t magic = readU16();
  uint32_t masterId = readU32();
  uint32_t sequence = readU32();
  uint32_t command = readU32();
  uint16_t receivedChecksum = readU16();
  uint16_t expectedChecksum = checksumPacket(magic, masterId, sequence, command);

  if (magic != PACKET_MAGIC ||
      masterId != MASTER_IG_ID ||
      receivedChecksum != expectedChecksum) {
    Serial.println(F("Paquete descartado: firma/master/checksum invalido"));
    return;
  }

  if (command != COMMAND_ACTIVATE) {
    Serial.println(F("Paquete descartado: comando desconocido"));
    return;
  }

  Serial.print(F("Comando de activacion recibido, seq="));
  Serial.println(sequence);

  if (state != READY) {
    sendStatus(sequence, STATUS_REMOTE_BUSY);
    Serial.println(F("Comando rechazado: remoto bloqueado u ocupado"));
    return;
  }

  beginCountdown(sequence, now);
}

void beginCountdown(uint32_t sequence, unsigned long now) {
  activeCommandSequence = sequence;
  lastCountdownPrintAt = 0;
  setState(COUNTDOWN, now);

  Serial.println(F("CUENTA REGRESIVA INICIADA"));
  sendStatus(sequence, STATUS_COUNTDOWN_STARTED);
  printCountdown(now);
}

void printCountdown(unsigned long now) {
  if (lastCountdownPrintAt != 0 &&
      !elapsed(now, lastCountdownPrintAt, COUNTDOWN_PRINT_INTERVAL_MS)) {
    return;
  }

  lastCountdownPrintAt = now;
  unsigned long elapsedMs = now - stateStartedAt;
  unsigned long remainingMs = elapsedMs >= COUNTDOWN_MS ? 0 : COUNTDOWN_MS - elapsedMs;
  unsigned long remainingSeconds = (remainingMs + 999) / 1000;

  Serial.print(F("Cuenta regresiva: "));
  Serial.print(remainingSeconds);
  Serial.println(F(" s"));
}

void beginSafeSimulation(unsigned long now) {
  digitalWrite(LED_EXEC_PIN, HIGH);
  setState(SIMULATING, now);
  Serial.println(F("EJECUCION SIMULADA INICIADA"));
  sendStatus(activeCommandSequence, STATUS_SAFE_ACTION_STARTED);
}

void finishSafeSimulation(unsigned long now) {
  digitalWrite(LED_EXEC_PIN, LOW);
  setState(COOLDOWN, now);
  Serial.println(F("EJECUCION SIMULADA TERMINADA"));
  Serial.println(F("Enfriamiento de 5 s"));
  sendStatus(activeCommandSequence, STATUS_SAFE_ACTION_DONE);
  activeCommandSequence = 0;
}

void sendStatus(uint32_t sequence, uint32_t statusCommand) {
  uint16_t packetChecksum = checksumPacket(
      PACKET_MAGIC,
      REMOTE_IG_ID,
      sequence,
      statusCommand);

  LoRa.beginPacket();
  writeU16(PACKET_MAGIC);
  writeU32(REMOTE_IG_ID);
  writeU32(sequence);
  writeU32(statusCommand);
  writeU16(packetChecksum);
  LoRa.endPacket();
  LoRa.receive();

  Serial.print(F("Estado enviado al master: 0x"));
  Serial.println(statusCommand, HEX);
}

void setState(SystemState nextState, unsigned long now) {
  state = nextState;
  stateStartedAt = now;
}

bool elapsed(unsigned long now, unsigned long since, unsigned long intervalMs) {
  return (unsigned long)(now - since) >= intervalMs;
}

void discardPacket() {
  while (LoRa.available()) {
    LoRa.read();
  }
}

uint16_t readU16() {
  uint16_t value = 0;
  value |= (uint16_t)LoRa.read();
  value |= (uint16_t)LoRa.read() << 8;
  return value;
}

uint32_t readU32() {
  uint32_t value = 0;
  value |= (uint32_t)LoRa.read();
  value |= (uint32_t)LoRa.read() << 8;
  value |= (uint32_t)LoRa.read() << 16;
  value |= (uint32_t)LoRa.read() << 24;
  return value;
}

void writeU16(uint16_t value) {
  LoRa.write((uint8_t)(value & 0xFF));
  LoRa.write((uint8_t)((value >> 8) & 0xFF));
}

void writeU32(uint32_t value) {
  LoRa.write((uint8_t)(value & 0xFF));
  LoRa.write((uint8_t)((value >> 8) & 0xFF));
  LoRa.write((uint8_t)((value >> 16) & 0xFF));
  LoRa.write((uint8_t)((value >> 24) & 0xFF));
}

uint16_t checksumPacket(uint16_t magic, uint32_t deviceId, uint32_t sequence, uint32_t command) {
  uint32_t mix = 0xA5A5;
  mix ^= magic;
  mix ^= deviceId;
  mix ^= deviceId >> 16;
  mix ^= sequence;
  mix ^= sequence >> 16;
  mix ^= command;
  mix ^= command >> 16;
  return (uint16_t)(mix & 0xFFFF);
}
