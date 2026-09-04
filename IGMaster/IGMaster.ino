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
const uint32_t COMMAND_SAFE_TEST = 0x51B7E20C;

const unsigned long MIN_SEND_INTERVAL_MS = 5000;

uint32_t packetSequence = 1;
unsigned long lastSendAt = 0;
String serialLine = "";

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

  Serial.println(F("master IG listo"));
  Serial.println(F("Escribe 51B7E20C y Enter para enviar una prueba segura"));
}

// ============================================================
void loop() {
  readSerialCommand();
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
  if (lastSendAt != 0 && (unsigned long)(now - lastSendAt) < MIN_SEND_INTERVAL_MS) {
    Serial.println(F("Envio bloqueado: espera 5 s antes de reenviar"));
    return;
  }

  sendSafeTestPacket();
  lastSendAt = now;
}

void sendSafeTestPacket() {
  uint16_t packetChecksum = checksumPacket(
      PACKET_MAGIC,
      MASTER_IG_ID,
      packetSequence,
      COMMAND_SAFE_TEST);

  LoRa.beginPacket();
  writeU16(PACKET_MAGIC);
  writeU32(MASTER_IG_ID);
  writeU32(packetSequence);
  writeU32(COMMAND_SAFE_TEST);
  writeU16(packetChecksum);
  LoRa.endPacket();

  Serial.print(F("Paquete master IG enviado, seq="));
  Serial.print(packetSequence);
  Serial.print(F(", comando=0x"));
  Serial.println(COMMAND_SAFE_TEST, HEX);

  packetSequence++;
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
