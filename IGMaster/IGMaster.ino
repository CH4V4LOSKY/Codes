// ============================================================
// IGMaster - ESP32, transmisor LoRa para prueba segura
//
// El usuario escribe Y en el Monitor Serial. Internamente el
// ESP32 envia un paquete LoRa con comando hexadecimal seguro.
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

// ---------- Protocolo de texto hexadecimal ----------
const char MASTER_PREFIX[] = "IGM";
const char REMOTE_PREFIX[] = "IGR";
const char MASTER_ID[] = "A71C5E2D";
const char REMOTE_ID[] = "9E771026";
const char COMMAND_ACTIVATE[] = "51B7E20C";
const char COMMAND_ACTIVATE_GUARD[] = "AE481DF3";
const char STATUS_COUNTDOWN_STARTED[] = "C0D15A7A";
const char STATUS_ACTION_STARTED[] = "E5EC0001";
const char STATUS_ACTION_DONE[] = "D04E0001";
const char STATUS_REMOTE_BUSY[] = "B105EADD";

const unsigned long MIN_SEND_INTERVAL_MS = 5000;
const unsigned long COMMAND_RETRY_INTERVAL_MS = 300;
const unsigned long CONFIRMATION_TIMEOUT_MS = 5000;

uint32_t packetSequence = 1;
String pendingSequence = "";
unsigned long lastSendAt = 0;
unsigned long pendingStartedAt = 0;
unsigned long lastRetryAt = 0;
bool waitingForRemoteConfirmation = false;

void readUserInput();
void startActivationRequest(unsigned long now);
void sendActivationPacket(const String &sequence);
void retryPendingCommand(unsigned long now);
void receiveLoRa(unsigned long now);
void handleRemotePacket(const String &packet);
bool parseRemotePacket(const String &packet, String &sequence, String &statusCode);
bool splitPacket(const String &packet, String fields[], uint8_t expectedCount);
String buildPacket(const char *prefix, const char *deviceId, const String &sequence, const char *code, const char *guard);
String guardForStatus(const String &code);
String sequenceToHex(uint32_t sequence);
void printRemoteStatus(const String &sequence, const String &statusCode);
void checkConfirmationTimeout(unsigned long now);
uint16_t crc16Text(const String &text);
uint16_t crc16Update(uint16_t crc, uint8_t data);
String hex16(uint16_t value);
bool elapsed(unsigned long now, unsigned long since, unsigned long intervalMs);

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
  Serial.println(F("Escribe Y para enviar el comando de activacion"));
}

// ============================================================
void loop() {
  unsigned long now = millis();

  readUserInput();
  receiveLoRa(now);
  retryPendingCommand(now);
  checkConfirmationTimeout(now);
}

void readUserInput() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == 'Y' || c == 'y') {
      startActivationRequest(millis());
      continue;
    }

    if (c == '\n' || c == '\r' || c == ' ') {
      continue;
    }

    Serial.println(F("Entrada ignorada. Escribe solo Y."));
  }
}

void startActivationRequest(unsigned long now) {
  if (lastSendAt != 0 && !elapsed(now, lastSendAt, MIN_SEND_INTERVAL_MS)) {
    Serial.println(F("Envio bloqueado: espera 5 s antes de reenviar"));
    return;
  }

  pendingSequence = sequenceToHex(packetSequence++);
  pendingStartedAt = now;
  lastRetryAt = now;
  lastSendAt = now;
  waitingForRemoteConfirmation = true;

  sendActivationPacket(pendingSequence);
  delay(80);
  sendActivationPacket(pendingSequence);
  delay(80);
  sendActivationPacket(pendingSequence);

  Serial.print(F("Y recibido. TX comando hex seguro, seq="));
  Serial.println(pendingSequence);
  Serial.println(F("Esperando: CUENTA REGRESIVA INICIADA"));
}

void sendActivationPacket(const String &sequence) {
  String packet = buildPacket(
      MASTER_PREFIX,
      MASTER_ID,
      sequence,
      COMMAND_ACTIVATE,
      COMMAND_ACTIVATE_GUARD);

  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
  LoRa.receive();

  Serial.print(F("TX LoRa: "));
  Serial.println(packet);
}

void retryPendingCommand(unsigned long now) {
  if (!waitingForRemoteConfirmation) {
    return;
  }

  if (!elapsed(now, lastRetryAt, COMMAND_RETRY_INTERVAL_MS)) {
    return;
  }

  lastRetryAt = now;
  sendActivationPacket(pendingSequence);
}

void receiveLoRa(unsigned long now) {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) {
    return;
  }

  String packet = "";
  while (LoRa.available()) {
    packet += (char)LoRa.read();
  }

  packet.trim();
  Serial.print(F("RX LoRa: "));
  Serial.println(packet);
  handleRemotePacket(packet);
}

void handleRemotePacket(const String &packet) {
  String sequence = "";
  String statusCode = "";

  if (!parseRemotePacket(packet, sequence, statusCode)) {
    Serial.println(F("Respuesta ignorada: no es estado valido del remoto"));
    return;
  }

  if (sequence == pendingSequence &&
      (statusCode == STATUS_COUNTDOWN_STARTED ||
       statusCode == STATUS_ACTION_STARTED ||
       statusCode == STATUS_ACTION_DONE ||
       statusCode == STATUS_REMOTE_BUSY)) {
    waitingForRemoteConfirmation = false;
  }

  printRemoteStatus(sequence, statusCode);
  Serial.print(F("RSSI="));
  Serial.print(LoRa.packetRssi());
  Serial.print(F(" dBm, SNR="));
  Serial.println(LoRa.packetSnr());
}

bool parseRemotePacket(const String &packet, String &sequence, String &statusCode) {
  String fields[6];
  if (!splitPacket(packet, fields, 6)) {
    return false;
  }

  String body = fields[0] + "|" + fields[1] + "|" + fields[2] + "|" + fields[3] + "|" + fields[4];
  String expectedCrc = hex16(crc16Text(body));

  if (fields[0] != REMOTE_PREFIX ||
      fields[1] != REMOTE_ID ||
      fields[4] != guardForStatus(fields[3]) ||
      fields[5] != expectedCrc) {
    return false;
  }

  sequence = fields[2];
  statusCode = fields[3];
  return sequence.length() == 8;
}

bool splitPacket(const String &packet, String fields[], uint8_t expectedCount) {
  int start = 0;

  for (uint8_t i = 0; i < expectedCount; i++) {
    int separator = packet.indexOf('|', start);

    if (i == expectedCount - 1) {
      if (separator != -1) {
        return false;
      }
      fields[i] = packet.substring(start);
      fields[i].trim();
      return fields[i].length() > 0;
    }

    if (separator == -1) {
      return false;
    }

    fields[i] = packet.substring(start, separator);
    fields[i].trim();
    start = separator + 1;
  }

  return false;
}

String buildPacket(const char *prefix, const char *deviceId, const String &sequence, const char *code, const char *guard) {
  String body = String(prefix) + "|" + deviceId + "|" + sequence + "|" + code + "|" + guard;
  return body + "|" + hex16(crc16Text(body));
}

String guardForStatus(const String &code) {
  if (code == STATUS_COUNTDOWN_STARTED) {
    return "3F2EA585";
  }
  if (code == STATUS_ACTION_STARTED) {
    return "1A13FFFE";
  }
  if (code == STATUS_ACTION_DONE) {
    return "2FB1FFFE";
  }
  if (code == STATUS_REMOTE_BUSY) {
    return "4EFA1522";
  }
  return "00000000";
}

String sequenceToHex(uint32_t sequence) {
  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%08lX", (unsigned long)sequence);
  return String(buffer);
}

void printRemoteStatus(const String &sequence, const String &statusCode) {
  Serial.print(F("IGRemote seq="));
  Serial.print(sequence);
  Serial.print(F(": "));

  if (statusCode == STATUS_COUNTDOWN_STARTED) {
    Serial.println(F("CUENTA REGRESIVA INICIADA"));
    return;
  }

  if (statusCode == STATUS_ACTION_STARTED) {
    Serial.println(F("LED D5 ENCENDIDO"));
    return;
  }

  if (statusCode == STATUS_ACTION_DONE) {
    Serial.println(F("LED D5 APAGADO"));
    return;
  }

  if (statusCode == STATUS_REMOTE_BUSY) {
    Serial.println(F("OCUPADO/BLOQUEADO"));
    return;
  }

  Serial.print(F("estado desconocido "));
  Serial.println(statusCode);
}

void checkConfirmationTimeout(unsigned long now) {
  if (!waitingForRemoteConfirmation) {
    return;
  }

  if (!elapsed(now, pendingStartedAt, CONFIRMATION_TIMEOUT_MS)) {
    return;
  }

  waitingForRemoteConfirmation = false;
  Serial.println(F("Sin confirmacion del remoto"));
}

uint16_t crc16Text(const String &text) {
  uint16_t crc = 0xFFFF;

  for (uint16_t i = 0; i < text.length(); i++) {
    crc = crc16Update(crc, (uint8_t)text[i]);
  }

  return crc;
}

uint16_t crc16Update(uint16_t crc, uint8_t data) {
  crc ^= (uint16_t)data << 8;

  for (uint8_t i = 0; i < 8; i++) {
    if ((crc & 0x8000) != 0) {
      crc = (crc << 1) ^ 0x1021;
    } else {
      crc <<= 1;
    }
  }

  return crc;
}

String hex16(uint16_t value) {
  char buffer[5];
  snprintf(buffer, sizeof(buffer), "%04X", value);
  return String(buffer);
}

bool elapsed(unsigned long now, unsigned long since, unsigned long intervalMs) {
  return (unsigned long)(now - since) >= intervalMs;
}
