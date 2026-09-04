// ============================================================
// IGMaster - transmisor LoRa para prueba segura de banco
//
// Escribe 51B7E20C en el Monitor Serial y presiona Enter para
// enviar el paquete binario al IGRemote seguro.
// ============================================================

#include <SPI.h>
#include <LoRa.h>

// ---------- Pines LoRa para ESP32 ----------
// Pines usados por los ejemplos LoRa_Tx/Rx de este repo.
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_SS 5
#define LORA_RST 14
#define LORA_DIO0 2

// ---------- Configuracion LoRa ----------
const long LORA_FREQUENCY = 433000000L;
const uint8_t LORA_SYNC_WORD = 0x34;

// ---------- Protocolo binario ----------
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

const unsigned long MIN_SEND_INTERVAL_MS = 5000;
const unsigned long PING_INTERVAL_MS = 2000;
const unsigned long ACK_WARNING_INTERVAL_MS = 5000;

const uint8_t PACKET_SIZE =
    sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t);

uint32_t packetSequence = 1;
unsigned long lastSafeSendAt = 0;
unsigned long lastPingAt = 0;
unsigned long lastAckAt = 0;
unsigned long lastAckWarningAt = 0;
String serialLine = "";

void readSerialCommand();
void handleSerialLine(const String &line);
void sendPeriodicPing(unsigned long now);
uint32_t sendPacket(uint32_t command);
void receiveLoRa(unsigned long now);
void printRemoteStatus(uint32_t command, uint32_t sequence);
void warnIfNoAck(unsigned long now);
bool elapsed(unsigned long now, unsigned long since, unsigned long intervalMs);
void discardPacket();
uint16_t readU16();
uint32_t readU32();
void writeU16(uint16_t value);
void writeU32(uint32_t value);
uint16_t checksumPacket(uint16_t magic, uint32_t masterId, uint32_t sequence, uint32_t command);

// ============================================================
void setup() {
  Serial.begin(9600);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println(F("ERROR: no se detecta el modulo LoRa"));
    while (true) {
      delay(1000);
    }
  }

  LoRa.setTxPower(17);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.enableCrc();
  LoRa.receive();

  Serial.println(F("master IG listo"));
  Serial.println(F("Escribe 51B7E20C y Enter para iniciar cuenta regresiva segura"));
  Serial.println(F("Modo diagnostico: enviando PING LoRa cada 2 s"));
}

// ============================================================
void loop() {
  unsigned long now = millis();

  readSerialCommand();
  sendPeriodicPing(now);
  receiveLoRa(now);
  warnIfNoAck(now);
}

void readSerialCommand() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n' || c == '\r') {
      serialLine.trim();
      serialLine.toUpperCase();

      if (serialLine.length() > 0) {
        handleSerialLine(serialLine);
      }

      serialLine = "";
      return;
    }

    if (serialLine.length() < 16) {
      serialLine += c;
    }
  }
}

void handleSerialLine(const String &line) {
  if (line != "51B7E20C") {
    Serial.println(F("Entrada ignorada: usa solo el codigo hexadecimal esperado"));
    return;
  }

  unsigned long now = millis();
  if (lastSafeSendAt != 0 && (unsigned long)(now - lastSafeSendAt) < MIN_SEND_INTERVAL_MS) {
    Serial.println(F("Envio bloqueado: espera 5 s antes de reenviar"));
    return;
  }

  uint32_t sequence = sendPacket(COMMAND_SAFE_TEST);
  lastSafeSendAt = now;

  Serial.print(F("Comando enviado; esperando confirmacion de cuenta regresiva, seq="));
  Serial.print(sequence);
  Serial.print(F(", comando=0x"));
  Serial.println(COMMAND_SAFE_TEST, HEX);
}

void sendPeriodicPing(unsigned long now) {
  if (lastPingAt != 0 && (unsigned long)(now - lastPingAt) < PING_INTERVAL_MS) {
    return;
  }

  lastPingAt = now;
  uint32_t sequence = sendPacket(COMMAND_LINK_PING);

  Serial.print(F("PING enviado, seq="));
  Serial.println(sequence);
}

uint32_t sendPacket(uint32_t command) {
  uint32_t sequence = packetSequence++;
  uint16_t packetChecksum = checksumPacket(
      PACKET_MAGIC,
      MASTER_IG_ID,
      sequence,
      command);

  LoRa.beginPacket();
  writeU16(PACKET_MAGIC);
  writeU32(MASTER_IG_ID);
  writeU32(sequence);
  writeU32(command);
  writeU16(packetChecksum);
  LoRa.endPacket();
  LoRa.receive();

  return sequence;
}

void receiveLoRa(unsigned long now) {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) {
    return;
  }

  Serial.print(F("Paquete recibido, bytes="));
  Serial.println(packetSize);

  if (packetSize != PACKET_SIZE) {
    discardPacket();
    Serial.println(F("Paquete descartado: longitud invalida"));
    return;
  }

  uint16_t magic = readU16();
  uint32_t remoteId = readU32();
  uint32_t sequence = readU32();
  uint32_t command = readU32();
  uint16_t receivedChecksum = readU16();
  uint16_t expectedChecksum = checksumPacket(magic, remoteId, sequence, command);

  if (magic != PACKET_MAGIC ||
      remoteId != REMOTE_IG_ID ||
      receivedChecksum != expectedChecksum) {
    Serial.println(F("Paquete descartado: firma/remoto/checksum invalido"));
    return;
  }

  lastAckAt = now;
  printRemoteStatus(command, sequence);
  Serial.print(F("RSSI="));
  Serial.print(LoRa.packetRssi());
  Serial.print(F(" dBm, SNR="));
  Serial.println(LoRa.packetSnr());
}

void printRemoteStatus(uint32_t command, uint32_t sequence) {
  Serial.print(F("IGRemote seq="));
  Serial.print(sequence);
  Serial.print(F(": "));

  if (command == COMMAND_LINK_ACK) {
    Serial.println(F("ACK de enlace"));
    return;
  }

  if (command == STATUS_COUNTDOWN_STARTED) {
    Serial.println(F("CUENTA REGRESIVA INICIADA"));
    return;
  }

  if (command == STATUS_SAFE_ACTION_STARTED) {
    Serial.println(F("EJECUCION SIMULADA INICIADA"));
    return;
  }

  if (command == STATUS_SAFE_ACTION_DONE) {
    Serial.println(F("EJECUCION SIMULADA TERMINADA"));
    return;
  }

  if (command == STATUS_REMOTE_BUSY) {
    Serial.println(F("OCUPADO/BLOQUEADO: no inicio cuenta regresiva"));
    return;
  }

  Serial.print(F("estado desconocido 0x"));
  Serial.println(command, HEX);
}

void warnIfNoAck(unsigned long now) {
  if (lastPingAt == 0) {
    return;
  }

  unsigned long lastLinkAt = lastAckAt == 0 ? lastPingAt : lastAckAt;
  if (!elapsed(now, lastLinkAt, ACK_WARNING_INTERVAL_MS)) {
    return;
  }

  if (!elapsed(now, lastAckWarningAt, ACK_WARNING_INTERVAL_MS)) {
    return;
  }

  lastAckWarningAt = now;
  Serial.println(F("Sin ACK todavia: revisar energia, GND comun, pines SPI y frecuencia 433 MHz"));
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
