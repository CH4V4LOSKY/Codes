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
const uint32_t COMMAND_SAFE_TEST = 0x51B7E20C;

const uint8_t PACKET_SIZE =
    sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t);

// ---------- Tiempos ----------
const unsigned long START_LOCK_MS = 5000;
const unsigned long SAFE_TEST_ON_MS = 500;
const unsigned long COOLDOWN_MS = 5000;

enum SystemState {
  START_LOCKED,
  READY,
  SIMULATING,
  COOLDOWN
};

SystemState state = START_LOCKED;
unsigned long stateStartedAt = 0;
uint32_t lastSequence = 0;
bool hasSequence = false;

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

  stateStartedAt = millis();
  Serial.println(F("IGRemote listo: bloqueo inicial de 5 s"));
  Serial.print(F("Comando hexadecimal de prueba segura: 0x"));
  Serial.println(COMMAND_SAFE_TEST, HEX);
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
    Serial.println(F("IGRemote abierto: esperando paquete valido"));
  }

  if (state == SIMULATING && elapsed(now, stateStartedAt, SAFE_TEST_ON_MS)) {
    digitalWrite(LED_BUILTIN, LOW);
    setState(COOLDOWN, now);
    Serial.println(F("Simulacion terminada; enfriamiento de 5 s"));
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
      command != COMMAND_SAFE_TEST ||
      receivedChecksum != expectedChecksum) {
    Serial.println(F("Paquete descartado: firma/comando/checksum invalido"));
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

  if (state != READY) {
    Serial.println(F("Comando valido ignorado: sistema bloqueado/en enfriamiento"));
    return;
  }

  beginSafeSimulation(now);
}

void beginSafeSimulation(unsigned long now) {
  digitalWrite(LED_BUILTIN, HIGH);
  setState(SIMULATING, now);
  Serial.println(F("SIMULACION segura activa por 500 ms"));
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
