#include <SPI.h>
#include <LoRa.h>

// ============================================================================
// Estacion Terrena - LoRa (con libreria LoRa.h)
// ----------------------------------------------------------------------------
// - Recibe la telemetria que envia la Computadora de Vuelo (CPV) por LoRa y
//   la muestra por el puerto serial.
// - Cuando el usuario escribe un comando en el Monitor Serial (por ejemplo
//   ARMAR o ACTIVAR) y presiona enter, este codigo lo envia por LoRa hacia la
//   CPV para que esta ejecute la accion correspondiente (armar el sistema /
//   activar el sistema y girar el motor). La logica de esas acciones vive del
//   lado de la CPV; aqui solo se transmite el comando.
// ============================================================================

// Pines LoRa (deben coincidir con la CPV)
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_NSS 5
#define LORA_RST 14
#define LORA_DIO0 2

#define LORA_FREQUENCY 433E6 // 433 MHz (ajustar si el modulo es de otra banda)

// ---------------------------------------------------------------------------
// Telemetria recibida: "TLM,sample,imuOk,accX,accY,accZ,gyroX,gyroY,gyroZ,
//                        imuTempC,magOk,headingDeg,baroOk,baroTempC,presionHpa,altitudM"
// ---------------------------------------------------------------------------
void emitirTelemetriaParaUi(const String &paquete, int rssi, float snr)
{
  Serial.print("UI_TLM,");
  Serial.print(paquete);
  Serial.print(",");
  Serial.print(rssi);
  Serial.print(",");
  Serial.println(snr, 1);
}

void mostrarTelemetria(String &paquete, int rssi, float snr)
{
  const int CAMPOS_ESPERADOS = 16; // TLM + 15 valores
  String campos[CAMPOS_ESPERADOS];
  int inicio = 0;
  int total = 0;

  for (int i = 0; i <= paquete.length() && total < CAMPOS_ESPERADOS; i++)
  {
    if (i == paquete.length() || paquete.charAt(i) == ',')
    {
      campos[total++] = paquete.substring(inicio, i);
      inicio = i + 1;
    }
  }

  if (total < CAMPOS_ESPERADOS || campos[0] != "TLM")
  {
    Serial.print("LoRa RX (paquete no reconocido): ");
    Serial.println(paquete);
    return;
  }

  unsigned long sample = campos[1].toInt();
  int imuOk = campos[2].toInt();
  float accX = campos[3].toFloat();
  float accY = campos[4].toFloat();
  float accZ = campos[5].toFloat();
  float gyroX = campos[6].toFloat();
  float gyroY = campos[7].toFloat();
  float gyroZ = campos[8].toFloat();
  float imuTempC = campos[9].toFloat();
  int magOk = campos[10].toInt();
  float headingDeg = campos[11].toFloat();
  int baroOk = campos[12].toInt();
  float baroTempC = campos[13].toFloat();
  float presionHpa = campos[14].toFloat();
  float altitudM = campos[15].toFloat();

  Serial.printf("#%lu ------------------------------------\n", sample);

  if (imuOk)
  {
    Serial.printf("IMU  | Acc[g]: X:%.2f Y:%.2f Z:%.2f | Gyro[dps]: X:%.1f Y:%.1f Z:%.1f | Temp: %.1f C\n",
                  accX, accY, accZ, gyroX, gyroY, gyroZ, imuTempC);
  }
  else
  {
    Serial.println("IMU  | Error de lectura (MPU6050)");
  }

  if (magOk)
  {
    Serial.printf("MAG  | Heading: %.1f deg\n", headingDeg);
  }
  else
  {
    Serial.println("MAG  | Error de lectura (HMC5883L)");
  }

  if (baroOk)
  {
    Serial.printf("BARO | Temp: %.1f C | Presion: %.1f hPa | Altitud: %.1f m\n",
                  baroTempC, presionHpa, altitudM);
  }
  else
  {
    Serial.println("BARO | Error de lectura (BMP180)");
  }

  Serial.printf("LoRa | RSSI: %d dBm | SNR: %.1f dB\n", rssi, snr);
}

void revisarTelemetriaLoRa()
{
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0)
  {
    return;
  }

  String paquete = "";
  while (LoRa.available())
  {
    paquete += (char)LoRa.read();
  }

  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();
  emitirTelemetriaParaUi(paquete, rssi, snr);
  mostrarTelemetria(paquete, rssi, snr);
}

// ---------------------------------------------------------------------------
// Comandos desde el puerto serial -> LoRa hacia la CPV
// ---------------------------------------------------------------------------
String serialBuffer = "";

void enviarComando(const String &comando)
{
  String paquete = "CMD:" + comando;

  LoRa.beginPacket();
  LoRa.print(paquete);
  LoRa.endPacket();

  Serial.print("Comando enviado a la CPV: ");
  Serial.println(comando);
}

void revisarComandosSerial()
{
  while (Serial.available())
  {
    char c = Serial.read();

    if (c == '\n' || c == '\r')
    {
      if (serialBuffer.length() > 0)
      {
        enviarComando(serialBuffer);
        serialBuffer = "";
      }
    }
    else
    {
      serialBuffer += c;
    }
  }
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY))
  {
    Serial.println("Error: no se detecto el modulo LoRa");
    while (true)
      ;
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);

  Serial.println("Estacion Terrena lista (libreria LoRa.h).");
  Serial.println("Escriba ARMAR o ACTIVAR en el Monitor Serial y presione enter para enviar el comando.");
}

void loop()
{
  revisarComandosSerial();
  revisarTelemetriaLoRa();
}
