// Arduino Nano clasico. Bibliotecas: Servo y LoRa (Sandeep Mistry).
// Servo D5; LoRa SCK D13, MISO D12, MOSI D11, NSS D10, RESET D9, DIO0 D2.
// RA-02: fuente de 3.3 V y adaptacion de nivel desde las salidas de 5 V.
// Servo con alimentacion apropiada y GND comun al Nano.
#include <SPI.h>
#include <LoRa.h>
#include <Servo.h>

Servo servo;

void setup() {
  Serial.begin(115200);
  Serial.println("INICIANDO SISTEMA");
  servo.write(0);
  servo.attach(5);

  SPI.begin();
  LoRa.setPins(10, 9, 2);
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
}

void loop() {
  if (!LoRa.parsePacket()) return;

  String comando = "";
  while (LoRa.available()) {
    comando += (char)LoRa.read();
  }

  if (comando == "ARMAR") {
    servo.write(0);
  } else if (comando == "ACTIVAR") {
    servo.write(90);
  }
}
