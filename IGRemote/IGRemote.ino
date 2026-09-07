// ============================================================
// IGRemote - Arduino Nano, receptor LoRa para prueba segura
//
// Recibe un paquete LoRa con campos hexadecimales, inicia una
// cuenta regresiva y enciende un LED externo en D5 por 500 ms.
// ============================================================

#include <SPI.h>
#include <LoRa.h>

// ---------- Pines LoRa para Arduino Nano ----------
// SPI hardware del Nano: SCK=D13, MISO=D12, MOSI=D11.
// D13 debe seguir conectado al SCK del modulo LoRa aunque el LED use D5.
#define LORA_SS 10
#define LORA_RST 9
#define LORA_DIO0 2

#define LED_EXEC_PIN 5

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

// ---------- Tiempos ----------
const unsigned long START_LOCK_MS = 5000;
const unsigned long COUNTDOWN_MS = 5000;
const unsigned long COUNTDOWN_PRINT_INTERVAL_MS = 1000;
const unsigned long LED_ON_MS = 1000;
const unsigned long COOLDOWN_MS = 5000;

enum SystemState {
  START_LOCKED,
  READY,
  COUNTDOWN,
  LED_ON,
  COOLDOWN
};

SystemState state = START_LOCKED;
unsigned long stateStartedAt = 0;
unsigned long lastCountdownPrintAt = 0;
String activeSequence = "";

void updateState(unsigned long now);
void receiveLoRa(unsigned long now);
void handlePacket(const String &packet, unsigned long now);
void beginCountdown(const String &sequence, unsigned long now);
void printCountdown(unsigned long now);
void beginLedPulse(unsigned long now);
void finishLedPulse(unsigned long now);
void sendStatus(const String &sequence, const char *statusCode);
bool parseActivationPacket(const String &packet, String &sequence);
bool splitPacket(const String &packet, String fields[], uint8_t expectedCount);
String buildPacket(const char *prefix, const char *deviceId, const String &sequence, const char *code);
String guardForCode(const char *code);
uint16_t crc16Text(const String &text);
uint16_t crc16Update(uint16_t crc, uint8_t data);
String hex16(uint16_t value);
bool elapsed(unsigned long now, unsigned long since, unsigned long intervalMs);
void setState(SystemState nextState, unsigned long now);

// ============================================================
void setup() {
  Serial.begin(9600);

  pinMode(LED_EXEC_PIN, OUTPUT);
  digitalWrite(LED_EXEC_PIN, LOW);

  SPI.begin();
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println(F("ERROR: no se detecta el modulo LoRa"));
    Serial.println(F("Verifica LoRa SCK=D13, MISO=D12, MOSI=D11, NSS=D10, RST=D9, DIO0=D2"));
    while (true) {
      digitalWrite(LED_EXEC_PIN, HIGH);
      delay(150);
      digitalWrite(LED_EXEC_PIN, LOW);
      delay(850);
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
  Serial.println(F("LoRa Nano: SCK=D13 MISO=D12 MOSI=D11 NSS=D10 RST=D9 DIO0=D2"));
  Serial.println(F("LED de ejecucion: D5"));
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
      beginLedPulse(now);
    }
  }

  if (state == LED_ON && elapsed(now, stateStartedAt, LED_ON_MS)) {
    finishLedPulse(now);
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

  String packet = "";
  while (LoRa.available()) {
    packet += (char)LoRa.read();
  }

  packet.trim();
  Serial.print(F("RX LoRa: "));
  Serial.println(packet);
  handlePacket(packet, now);
}

void handlePacket(const String &packet, unsigned long now) {
  String sequence = "";
  if (!parseActivationPacket(packet, sequence)) {
    Serial.println(F("Paquete ignorado: no es comando valido"));
    return;
  }

  Serial.print(F("Comando valido recibido, seq="));
  Serial.println(sequence);

  if (state == COUNTDOWN && sequence == activeSequence) {
    sendStatus(sequence, STATUS_COUNTDOWN_STARTED);
    Serial.println(F("Reenvio recibido: cuenta regresiva ya iniciada"));
    return;
  }

  if (state == LED_ON && sequence == activeSequence) {
    sendStatus(sequence, STATUS_ACTION_STARTED);
    Serial.println(F("Reenvio recibido: LED ya encendido"));
    return;
  }

  if (state != READY) {
    sendStatus(sequence, STATUS_REMOTE_BUSY);
    Serial.println(F("Comando rechazado: remoto bloqueado u ocupado"));
    return;
  }

  beginCountdown(sequence, now);
}

void beginCountdown(const String &sequence, unsigned long now) {
  activeSequence = sequence;
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
  sendStatus(activeSequence, STATUS_COUNTDOWN_STARTED);
}

void beginLedPulse(unsigned long now) {
  digitalWrite(LED_EXEC_PIN, HIGH);
  setState(LED_ON, now);
  Serial.println(F("LED D5 ENCENDIDO"));
  sendStatus(activeSequence, STATUS_ACTION_STARTED);
}

void finishLedPulse(unsigned long now) {
  digitalWrite(LED_EXEC_PIN, LOW);
  setState(COOLDOWN, now);
  Serial.println(F("LED D5 APAGADO"));
  Serial.println(F("Enfriamiento de 5 s"));
  sendStatus(activeSequence, STATUS_ACTION_DONE);
  activeSequence = "";
}

void sendStatus(const String &sequence, const char *statusCode) {
  String response = buildPacket(REMOTE_PREFIX, REMOTE_ID, sequence, statusCode);

  LoRa.beginPacket();
  LoRa.print(response);
  LoRa.endPacket();
  LoRa.receive();

  Serial.print(F("TX estado: "));
  Serial.println(response);
}

bool parseActivationPacket(const String &packet, String &sequence) {
  String fields[6];
  if (!splitPacket(packet, fields, 6)) {
    return false;
  }

  String body = fields[0] + "|" + fields[1] + "|" + fields[2] + "|" + fields[3] + "|" + fields[4];
  String expectedCrc = hex16(crc16Text(body));

  if (fields[0] != MASTER_PREFIX ||
      fields[1] != MASTER_ID ||
      fields[3] != COMMAND_ACTIVATE ||
      fields[4] != COMMAND_ACTIVATE_GUARD ||
      fields[5] != expectedCrc) {
    return false;
  }

  sequence = fields[2];
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

String buildPacket(const char *prefix, const char *deviceId, const String &sequence, const char *code) {
  String body = String(prefix) + "|" + deviceId + "|" + sequence + "|" + code + "|" + guardForCode(code);
  return body + "|" + hex16(crc16Text(body));
}

String guardForCode(const char *code) {
  if (strcmp(code, STATUS_COUNTDOWN_STARTED) == 0) {
    return "3F2EA585";
  }
  if (strcmp(code, STATUS_ACTION_STARTED) == 0) {
    return "1A13FFFE";
  }
  if (strcmp(code, STATUS_ACTION_DONE) == 0) {
    return "2FB1FFFE";
  }
  if (strcmp(code, STATUS_REMOTE_BUSY) == 0) {
    return "4EFA1522";
  }
  return "00000000";
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

void setState(SystemState nextState, unsigned long now) {
  state = nextState;
  stateStartedAt = now;
}
