// ============================================================
// IGRemote - receptor LoRa para prueba segura de banco
//
// Este sketch NO energiza ningun relay ni salida de ignicion.
// Solo simula una activacion durante 500 ms con Serial y LED integrado.
// ============================================================

#include <SPI.h>
#include <LoRa.h>

// ---------- Pines LoRa para Arduino Nano ----------
// SPI hardware del Nano: SCK=D13, MISO=D12, MOSI=D11.
#define LORA_SS 10
#define LORA_RST 9
#define LORA_DIO0 2

#ifndef LED_BUILTIN
#define LED_BUILTIN 13
#endif

// ---------- Configuracion LoRa ----------
const long LORA_FREQUENCY = 433000000L;
const uint8_t LORA_SYNC_WORD = 0x34;

// ---------- Protocolo binario ----------
// Todos los valores se mandan como bytes, no como palabras de texto.
const uint16_t PACKET_MAGIC = 0xC0DE;
const uint32_t MASTER_IG_ID = 0xA71C5E2D;
const uint32_t REMOTE_IG_ID = 0x9E771026;
const uint32_t COMMAND_SAFE_TEST = 0x51B7E20C;
const uint32_t COMMAND_LINK_PING = 0x13579BDF;
const uint32_t COMMAND_LINK_ACK = 0xACCE5501;
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
const unsigned long SAFE_TEST_ON_MS = 500;
const unsigned long COOLDOWN_MS = 5000;
const unsigned long STATUS_INTERVAL_MS = 3000;

enum SystemState {
  START_LOCKED,
  READY,
  COUNTDOWN,
  SIMULATING,
  COOLDOWN
};

SystemState state = START_LOCKED;
unsigned long stateStartedAt = 0;
uint32_t lastSequence = 0;
bool hasSequence = false;
unsigned long lastStatusAt = 0;
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
void printStatus(unsigned long now);
const __FlashStringHelper *stateName(SystemState currentState);
void discardPacket();
uint16_t readU16();
uint32_t readU32();
void writeU16(uint16_t value);
void writeU32(uint32_t value);
uint16_t checksumPacket(uint16_t magic, uint32_t masterId, uint32_t sequence, uint32_t command);

// ============================================================
void setup() {
  Serial.begin(9600);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

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
  Serial.println(F("IGRemote listo: bloqueo inicial de 5 s"));
  Serial.print(F("Comando hexadecimal de prueba segura: 0x"));
  Serial.println(COMMAND_SAFE_TEST, HEX);
  Serial.println(F("Al recibir el comando valido inicia cuenta regresiva segura de 5 s"));
  Serial.println(F("Modo diagnostico: esperando PING del master IG"));
}

// ============================================================
void loop() {
  unsigned long now = millis();

  updateState(now);
  receiveLoRa(now);
  printStatus(now);
}

void updateState(unsigned long now) {
  if (state == START_LOCKED && elapsed(now, stateStartedAt, START_LOCK_MS)) {
    setState(READY, now);
    Serial.println(F("IGRemote abierto: esperando paquete valido"));
  }

  if (state == COUNTDOWN) {
    printCountdown(now);

    if (elapsed(now, stateStartedAt, COUNTDOWN_MS)) {
      beginSafeSimulation(now);
    }
  }

  if (state == SIMULATING && elapsed(now, stateStartedAt, SAFE_TEST_ON_MS)) {
    finishSafeSimulation(now);
  }

  if (state == COOLDOWN && elapsed(now, stateStartedAt, COOLDOWN_MS)) {
    setState(READY, now);
    Serial.println(F("IGRemote listo para otro paquete valido"));
  }
}

void receiveLoRa(unsigned long now) {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) {
    return;
  }

  Serial.print(F("Paquete detectado, bytes="));
  Serial.println(packetSize);

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

  if (hasSequence && sequence <= lastSequence) {
    Serial.println(F("Paquete descartado: secuencia repetida o anterior"));
    return;
  }

  lastSequence = sequence;
  hasSequence = true;

  Serial.print(F("Paquete valido de master IG, seq="));
  Serial.print(sequence);
  Serial.print(F(", RSSI="));
  Serial.print(LoRa.packetRssi());
  Serial.print(F(" dBm, SNR="));
  Serial.println(LoRa.packetSnr());

  if (command == COMMAND_LINK_PING) {
    Serial.println(F("PING recibido; enviando ACK"));
    sendStatus(sequence, COMMAND_LINK_ACK);
    return;
  }

  if (command != COMMAND_SAFE_TEST) {
    Serial.println(F("Paquete descartado: comando desconocido"));
    sendStatus(sequence, COMMAND_LINK_ACK);
    return;
  }

  if (state != READY) {
    sendStatus(sequence, STATUS_REMOTE_BUSY);
    Serial.println(F("Comando valido ignorado: sistema bloqueado/en enfriamiento"));
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

  Serial.print(F("Cuenta regresiva segura: "));
  Serial.print(remainingSeconds);
  Serial.println(F(" s"));
}

void beginSafeSimulation(unsigned long now) {
  digitalWrite(LED_BUILTIN, HIGH);
  setState(SIMULATING, now);
  Serial.println(F("SIMULACION segura ejecutandose por 500 ms"));
  sendStatus(activeCommandSequence, STATUS_SAFE_ACTION_STARTED);
}

void finishSafeSimulation(unsigned long now) {
  digitalWrite(LED_BUILTIN, LOW);
  setState(COOLDOWN, now);
  Serial.println(F("Simulacion terminada; enfriamiento de 5 s"));
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

  Serial.print(F("Estado enviado al master, seq="));
  Serial.print(sequence);
  Serial.print(F(", status=0x"));
  Serial.println(statusCommand, HEX);
}

void setState(SystemState nextState, unsigned long now) {
  state = nextState;
  stateStartedAt = now;
}

bool elapsed(unsigned long now, unsigned long since, unsigned long intervalMs) {
  return (unsigned long)(now - since) >= intervalMs;
}

void printStatus(unsigned long now) {
  if (!elapsed(now, lastStatusAt, STATUS_INTERVAL_MS)) {
    return;
  }

  lastStatusAt = now;
  Serial.print(F("Estado IGRemote: "));
  Serial.println(stateName(state));
}

const __FlashStringHelper *stateName(SystemState currentState) {
  switch (currentState) {
    case START_LOCKED:
      return F("BLOQUEO_INICIAL");
    case READY:
      return F("LISTO");
    case COUNTDOWN:
      return F("CUENTA_REGRESIVA");
    case SIMULATING:
      return F("SIMULANDO");
    case COOLDOWN:
      return F("ENFRIAMIENTO");
  }

  return F("DESCONOCIDO");
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

uint16_t checksumPacket(uint16_t magic, uint32_t masterId, uint32_t sequence, uint32_t command) {
  uint32_t mix = 0xA5A5;
  mix ^= magic;
  mix ^= masterId;
  mix ^= masterId >> 16;
  mix ^= sequence;
  mix ^= sequence >> 16;
  mix ^= command;
  mix ^= command >> 16;
  return (uint16_t)(mix & 0xFFFF);
}
