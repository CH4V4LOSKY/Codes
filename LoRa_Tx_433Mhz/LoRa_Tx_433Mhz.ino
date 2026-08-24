#include <SPI.h>
#include <LoRa.h>

// LoRa Tx 433MHz
float accx;

// Firma para identificar paquetes propios y filtrar ruido/otros equipos
const uint16_t PACKET_MAGIC = 0xC0DE;
// Numero de secuencia incremental: permite al receptor detectar paquetes perdidos
uint32_t packetSeq = 0;

// Pines LoRa
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_NSS 5
#define LORA_RST 14
#define LORA_DIO0 2

void setup()
{

  Serial.begin(9600);

  // Configurar SPI
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  // Configurar pines del LoRa
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);

  // Iniciar LoRa a 433 MHz
  if (!LoRa.begin(433E6))
  {
    Serial.println("Error al iniciar LoRa");
    while (true)
      ;
  }

  Serial.println("LoRa TX iniciado");

  // Potencia de transmisión
  LoRa.setTxPower(17);

  // Parámetros LoRa
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);

  Serial.println("Transmisor listo");
}

void loop()
{

  accx = accx + 1;

  Serial.printf("Enviando paquete #%lu...\n", (unsigned long)packetSeq);

  LoRa.beginPacket();

  // Cabecera: firma + numero de secuencia (para deteccion de perdidas en el Rx)
  LoRa.write((uint8_t *)&PACKET_MAGIC, sizeof(PACKET_MAGIC));
  LoRa.write((uint8_t *)&packetSeq, sizeof(packetSeq));

  if (accx <= 20)
  {
    LoRa.printf("Hola desde el cohete = %.2f", accx);
  }
  else
  {
    LoRa.printf("Ya valio aaaaaaaaaaa = %.2f", accx);
  }

  LoRa.endPacket();

  Serial.println("Paquete enviado");

  packetSeq++;

  delay(1000);
}
