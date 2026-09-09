/*
  code_Fredy: receptor LoRa para un servo posicional en D14 / GPIO14.
  Bibliotecas Arduino: ESP32Servo y LoRa (Sandeep Mistry).
  UI -> USB -> EstacionTerrena_LoRa_Lib -> LoRa -> code_Fredy.
  ARMAR = 0 grados; ACTIVAR = 90 grados. Inicio: 0 grados.

  Cableado del receptor ESP32 clasico:
  Servo: senal GPIO14, alimentacion externa apropiada y GND comun al ESP32.
  No alimentar el servo desde 3.3 V del ESP32.
  LoRa: SCK 18, MISO 19, MOSI 23, NSS 5, RESET 27, DIO0 2.
  IMPORTANTE: mover RESET del LoRa de GPIO14 a GPIO27 en ESTE receptor.
  La estacion terrena conserva su cableado y su programa actuales.

  Pulsos de 1000 a 2000 us: ajustar segun la ficha del servo para calibrar
  los angulos reales. Este programa no mide la posicion fisica del servo.
  Monitor serie: 115200 baudios.
*/
#include <SPI.h>
#include <LoRa.h>
#include <ESP32Servo.h>

constexpr int SERVO_PIN = 14;
constexpr int LORA_SCK = 18;
constexpr int LORA_MISO = 19;
constexpr int LORA_MOSI = 23;
constexpr int LORA_NSS = 5;
constexpr int LORA_RST = 27;
constexpr int LORA_DIO0 = 2;
constexpr long LORA_FREQUENCY = 433000000;
constexpr int PULSO_MIN_US = 1000;
constexpr int PULSO_MAX_US = 2000;

Servo servo;

void procesarComando(const String &paquete) {
  // Protocolo de la estacion: CMD:<seq>:<comando>.
  if (!paquete.startsWith("CMD:")) return;
  int separador = paquete.indexOf(':', 4);
  if (separador <= 4) return;
  String secuencia = paquete.substring(4, separador);
  for (unsigned int i = 0; i < secuencia.length(); ++i) {
    if (secuencia[i] < '0' || secuencia[i] > '9') return;
  }
  String comando = paquete.substring(separador + 1);
  int angulo;
  if (comando == "ARMAR") {
    angulo = 0;
  } else if (comando == "ACTIVAR") {
    angulo = 90;
  } else {
    Serial.println("Comando desconocido: " + comando);
    return;
  }

  // Asignar una posicion absoluta hace inocuos los reintentos de la estacion,
  // incluso si esta reinicia su contador de secuencia al reconectarse.
  servo.write(angulo);
  Serial.print(comando);
  Serial.print(" -> posicion solicitada: ");
  Serial.println(angulo);

  // Confirma la orden aplicada al PWM, no una medicion de posicion fisica.
  LoRa.beginPacket();
  LoRa.print("ACK:");
  LoRa.print(secuencia);
  LoRa.print(":");
  LoRa.print(comando);
  LoRa.endPacket();
}

void setup() {
  Serial.begin(115200);
  servo.setPeriodHertz(50);
  servo.attach(SERVO_PIN, PULSO_MIN_US, PULSO_MAX_US);
  if (!servo.attached()) {
    Serial.println("Error al iniciar servo en GPIO14.");
    while (true) delay(1000);
  }
  servo.write(0);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("Error LoRa: revisar conexiones, incluido RESET en GPIO27.");
    while (true) delay(1000);
  }
  // Mismos parametros que EstacionTerrena_LoRa_Lib.
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  Serial.println("code_Fredy listo: ARMAR = 0 grados; ACTIVAR = 90 grados.");
}

void loop() {
  if (LoRa.parsePacket() == 0) return;
  String paquete;
  while (LoRa.available()) paquete += (char)LoRa.read();
  procesarComando(paquete);
}
