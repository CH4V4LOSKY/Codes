//  ============================================================
 // CIRCUITO PRINCIPAL - Arduino Nano


#include <SPI.h>
#include <LoRa.h>

// ---------- Pines ----------
#define LORA_SS   10
#define LORA_RST  9
#define LORA_DIO0 2

#define RELAY_PIN 4

// ---------- Configuración ----------
#define RELAY_ACTIVE_LOW false          // KY-019 es activo en HIGH
const unsigned long TRIGGER_INTERVAL = 5000; // ms desde el arranque para activar el relé 
const unsigned long RELAY_ON_TIME    = 500;  // cuánto tiempo se queda encendido 

// ---------- Variables ----------
bool yaActivado = false;      // controla que el relé solo se dispare una vez
unsigned long relayOnSince = 0;
bool relayIsOn = false;

unsigned long lastLoraSend = 0;
const unsigned long LORA_SEND_INTERVAL = 10000; // envía estado cada 10s

// ============================================================
void setup() {
  Serial.begin(9600);

  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false); // arranca apagado

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {  // (433MHz)
    Serial.println(F("ERROR: no se detecta el módulo LoRa"));
  } else {
    Serial.println(F("LoRa listo"));
  }

  Serial.println(F("Sistema iniciado"));
}

// ============================================================
void loop() {
  unsigned long currentMillis = millis();

  // --- Enciende el relé UNA SOLA VEZ, a los TRIGGER_INTERVAL ms de arrancar ---
  if (!yaActivado && (currentMillis >= TRIGGER_INTERVAL)) {
    setRelay(true);
    relayIsOn = true;
    relayOnSince = currentMillis;
    yaActivado = true; // bloquea futuras activaciones
    Serial.println(F("Relé activado (única vez)"));
  }

  // --- Apaga el relé después de RELAY_ON_TIME ---
  if (relayIsOn && (currentMillis - relayOnSince >= RELAY_ON_TIME)) {
    setRelay(false);
    relayIsOn = false;
    Serial.println(F("Relé apagado"));
  }

  // --- Envía estado por LoRa cada LORA_SEND_INTERVAL (esto sí se repite siempre) ---
  if (currentMillis - lastLoraSend >= LORA_SEND_INTERVAL) {
    lastLoraSend = currentMillis;
    enviarEstadoLoRa();
  }

  // --- Revisa si llegó algo por LoRa ---
  recibirLoRa();
}

// ============================================================
void setRelay(bool encender) {
  bool nivel = RELAY_ACTIVE_LOW ? !encender : encender;
  digitalWrite(RELAY_PIN, nivel ? HIGH : LOW);
}

void enviarEstadoLoRa() {
  LoRa.beginPacket();
  LoRa.print("UPTIME_MS:");
  LoRa.print(millis());
  LoRa.print(",RELAY:");
  LoRa.print(relayIsOn ? 1 : 0);
  LoRa.print(",ACTIVADO_YA:");
  LoRa.print(yaActivado ? 1 : 0);
  LoRa.endPacket();
}

void recibirLoRa() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String mensaje = "";
    while (LoRa.available()) {
      mensaje += (char)LoRa.read();
    }
    Serial.print(F("LoRa recibido: "));
    Serial.println(mensaje);
  }
}