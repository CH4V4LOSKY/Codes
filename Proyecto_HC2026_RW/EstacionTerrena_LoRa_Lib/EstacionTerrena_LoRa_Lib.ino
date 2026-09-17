// Estacion ESP32 clasico. Biblioteca: LoRa (Sandeep Mistry).
// LoRa SCK 18, MISO 19, MOSI 23, NSS 5, RESET 14, DIO0 2.
// Monitor serial: 115200 baudios, con nueva linea o retorno de carro.
#include <SPI.h>
#include <LoRa.h>

String comando = "";

void setup() {
  Serial.begin(115200);
  SPI.begin(18, 19, 23, 5);
  LoRa.setPins(5, 14, 2);
  if (!LoRa.begin(433000000)) {
    Serial.println("Error al iniciar LoRa");
    while (true) { delay(1000); }
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.setPreambleLength(8);
  LoRa.disableCrc();
  LoRa.disableInvertIQ();
  Serial.println("Escriba ARMAR o ACTIVAR y presione enter.");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      comando.trim();
      comando.toUpperCase();
      if (comando == "ARMAR" || comando == "ACTIVAR") {
        LoRa.beginPacket();
        LoRa.print(comando);
        LoRa.endPacket();
        Serial.print("Enviado: ");
        Serial.println(comando);
      }
      comando = "";
    } else {
      // Limitar la memoria incluso si no se recibe un fin de linea.
      if (comando.length() < 32) comando += c;
    }
  }
}
