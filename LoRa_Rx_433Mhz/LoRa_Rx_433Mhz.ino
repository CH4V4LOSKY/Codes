#include <SPI.h>
#include <LoRa.h>

// LoRa Rx 433MHz
// Pines LoRa
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_NSS 5
#define LORA_RST 14
#define LORA_DIO0 2

// Debe coincidir con la firma usada en el Tx
const uint16_t PACKET_MAGIC = 0xC0DE;

// Estadisticas de recepcion
uint32_t expectedSeq = 0;
bool firstPacket = true;
uint32_t receivedCount = 0;
uint32_t lostCount = 0;

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

  Serial.println("LoRa RX iniciado");

  // Parametros LoRa (deben coincidir con el Tx)
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);

  Serial.println("Receptor listo");
}

void loop()
{
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0)
  {
    return;
  }

  // Cabecera minima: firma (2 bytes) + secuencia (4 bytes)
  if (packetSize < (int)(sizeof(PACKET_MAGIC) + sizeof(uint32_t)))
  {
    Serial.println("Paquete descartado: demasiado corto");
    return;
  }

  uint16_t magic = 0;
  uint32_t seq = 0;
  LoRa.readBytes((uint8_t *)&magic, sizeof(magic));
  LoRa.readBytes((uint8_t *)&seq, sizeof(seq));

  if (magic != PACKET_MAGIC)
  {
    Serial.println("Paquete descartado: firma invalida");
    // Vaciar el resto del paquete
    while (LoRa.available())
      LoRa.read();
    return;
  }

  // Resto del paquete: mensaje de texto
  String message = "";
  while (LoRa.available())
  {
    message += (char)LoRa.read();
  }

  receivedCount++;

  if (firstPacket)
  {
    expectedSeq = seq;
    firstPacket = false;
  }

  if (seq > expectedSeq)
  {
    uint32_t missing = seq - expectedSeq;
    lostCount += missing;
    Serial.printf("!! Se perdieron %lu paquete(s) (esperaba #%lu, llego #%lu)\n",
                   (unsigned long)missing, (unsigned long)expectedSeq, (unsigned long)seq);
  }
  else if (seq < expectedSeq)
  {
    Serial.printf("Paquete #%lu fuera de orden o duplicado\n", (unsigned long)seq);
  }

  expectedSeq = seq + 1;

  float lossPercent = (receivedCount + lostCount) > 0
                           ? (100.0f * lostCount) / (receivedCount + lostCount)
                           : 0.0f;

  Serial.printf("Paquete #%lu | RSSI: %d dBm | SNR: %.1f dB | %s\n",
                (unsigned long)seq, LoRa.packetRssi(), LoRa.packetSnr(), message.c_str());
  Serial.printf("Recibidos: %lu | Perdidos: %lu | Perdida: %.1f%%\n",
                (unsigned long)receivedCount, (unsigned long)lostCount, lossPercent);
}
