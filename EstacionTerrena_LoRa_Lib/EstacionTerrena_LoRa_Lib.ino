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

  LoRa.setSyncWord(0x12); // Clave para sincronización "0x12" : Pareja 1 (CPV y ET)

  Serial.println("Estacion Terrena lista (libreria LoRa.h) - PAREJA 1.");

//  Serial.println("Estacion Terrena lista (libreria LoRa.h).");
  Serial.println("Escriba ARMAR o ACTIVAR en el Monitor Serial y presione enter para enviar el comando.");
}

void loop()
{
  revisarComandosSerial();
  revisarPaquetesLoRa();
  revisarReintentosComando();
}
