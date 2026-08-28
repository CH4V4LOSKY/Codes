#include <SPI.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ============================================================================
// Estacion Terrena - LoRa SIN LIBRERIAS
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

#define LORA_FREQUENCY 433000000UL // 433 MHz (ajustar si el modulo es de otra banda)

// Registros del SX127x usados
#define REG_FIFO 0x00
#define REG_OP_MODE 0x01
#define REG_FRF_MSB 0x06
#define REG_FRF_MID 0x07
#define REG_FRF_LSB 0x08
#define REG_PA_CONFIG 0x09
#define REG_LNA 0x0C
#define REG_FIFO_ADDR_PTR 0x0D
#define REG_FIFO_TX_BASE_ADDR 0x0E
#define REG_FIFO_RX_BASE_ADDR 0x0F
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS 0x12
#define REG_RX_NB_BYTES 0x13
#define REG_PKT_SNR_VALUE 0x19
#define REG_PKT_RSSI_VALUE 0x1A
#define REG_MODEM_CONFIG_1 0x1D
#define REG_MODEM_CONFIG_2 0x1E
#define REG_PREAMBLE_MSB 0x20
#define REG_PREAMBLE_LSB 0x21
#define REG_PAYLOAD_LENGTH 0x22
#define REG_MODEM_CONFIG_3 0x26
#define REG_VERSION 0x42

#define MODE_LONG_RANGE_MODE 0x80
#define MODE_SLEEP 0x00
#define MODE_STDBY 0x01
#define MODE_TX 0x03
#define MODE_RX_CONTINUOUS 0x05

#define IRQ_TX_DONE_MASK 0x08
#define IRQ_RX_DONE_MASK 0x40
#define IRQ_PAYLOAD_CRC_ERROR_MASK 0x20

// ---------------------------------------------------------------------------
// LoRa a registros crudos (sin libreria LoRa.h)
// ---------------------------------------------------------------------------
void loraWriteRegister(uint8_t reg, uint8_t value)
{
  digitalWrite(LORA_NSS, LOW);
  SPI.transfer(reg | 0x80); // bit7 = 1 -> escritura
  SPI.transfer(value);
  digitalWrite(LORA_NSS, HIGH);
}

uint8_t loraReadRegister(uint8_t reg)
{
  digitalWrite(LORA_NSS, LOW);
  SPI.transfer(reg & 0x7F); // bit7 = 0 -> lectura
  uint8_t value = SPI.transfer(0x00);
  digitalWrite(LORA_NSS, HIGH);
  return value;
}

void loraReset()
{
  digitalWrite(LORA_RST, LOW);
  delay(10);
  digitalWrite(LORA_RST, HIGH);
  delay(10);
}

void loraSetFrequency(uint32_t freqHz)
{
  uint64_t frf = ((uint64_t)freqHz << 19) / 32000000ULL;
  loraWriteRegister(REG_FRF_MSB, (uint8_t)(frf >> 16));
  loraWriteRegister(REG_FRF_MID, (uint8_t)(frf >> 8));
  loraWriteRegister(REG_FRF_LSB, (uint8_t)(frf >> 0));
}

void loraStandby()
{
  loraWriteRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
}

void loraSleep()
{
  loraWriteRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
}

bool loraInit()
{
  pinMode(LORA_NSS, OUTPUT);
  pinMode(LORA_RST, OUTPUT);
  pinMode(LORA_DIO0, INPUT);
  digitalWrite(LORA_NSS, HIGH);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  loraReset();

  uint8_t version = loraReadRegister(REG_VERSION);
  if (version != 0x12)
  {
    return false; // No se detecto un SX1276/77/78/79
  }

  loraSleep();
  loraSetFrequency(LORA_FREQUENCY);

  // Punteros base del FIFO (Tx y Rx comparten todo el buffer, 0x00..0xFF)
  loraWriteRegister(REG_FIFO_TX_BASE_ADDR, 0x00);
  loraWriteRegister(REG_FIFO_RX_BASE_ADDR, 0x00);

  loraWriteRegister(REG_LNA, loraReadRegister(REG_LNA) | 0x03); // LNA boost

  loraWriteRegister(REG_MODEM_CONFIG_1, 0x72); // BW 125 kHz, CR 4/5, cabecera explicita
  loraWriteRegister(REG_MODEM_CONFIG_2, 0x74); // SF7, CRC activado
  loraWriteRegister(REG_MODEM_CONFIG_3, 0x04); // AGC automatico

  loraWriteRegister(REG_PREAMBLE_MSB, 0x00);
  loraWriteRegister(REG_PREAMBLE_LSB, 0x08);

  loraWriteRegister(REG_PA_CONFIG, 0x8F); // PA_BOOST, ~17 dBm

  loraStandby();
  return true;
}

void loraSend(const uint8_t *data, uint8_t len)
{
  loraStandby();

  loraWriteRegister(REG_FIFO_ADDR_PTR, 0x00);
  for (uint8_t i = 0; i < len; i++)
  {
    loraWriteRegister(REG_FIFO, data[i]);
  }
  loraWriteRegister(REG_PAYLOAD_LENGTH, len);

  loraWriteRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX);

  uint32_t startMillis = millis();
  while ((loraReadRegister(REG_IRQ_FLAGS) & IRQ_TX_DONE_MASK) == 0)
  {
    if (millis() - startMillis > 2000)
    {
      break; // Seguridad: no bloquear para siempre si algo falla
    }
  }

  loraWriteRegister(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK); // Se limpia escribiendo 1 en el bit
  loraStandby();
}

void loraStartReceive()
{
  loraWriteRegister(REG_FIFO_ADDR_PTR, 0x00);
  loraWriteRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
}

// Devuelve la cantidad de bytes leidos (0 = nada nuevo, -1 = error de CRC)
int loraReceivePacket(uint8_t *buffer, uint8_t maxLen)
{
  uint8_t irqFlags = loraReadRegister(REG_IRQ_FLAGS);
  if ((irqFlags & IRQ_RX_DONE_MASK) == 0)
  {
    return 0;
  }

  loraWriteRegister(REG_IRQ_FLAGS, 0xFF); // Limpiar todas las banderas

  if (irqFlags & IRQ_PAYLOAD_CRC_ERROR_MASK)
  {
    return -1;
  }

  uint8_t len = loraReadRegister(REG_RX_NB_BYTES);
  if (len > maxLen)
  {
    len = maxLen;
  }

  uint8_t currentAddr = loraReadRegister(REG_FIFO_RX_CURRENT_ADDR);
  loraWriteRegister(REG_FIFO_ADDR_PTR, currentAddr);

  for (uint8_t i = 0; i < len; i++)
  {
    buffer[i] = loraReadRegister(REG_FIFO);
  }

  return len;
}

int loraPacketRssi()
{
  return (int)loraReadRegister(REG_PKT_RSSI_VALUE) - 164; // Puerto de baja frecuencia (<868 MHz)
}

float loraPacketSnr()
{
  return ((int8_t)loraReadRegister(REG_PKT_SNR_VALUE)) * 0.25f;
}

// ---------------------------------------------------------------------------
// Telemetria recibida: "TLM,sample,imuOk,accX,accY,accZ,gyroX,gyroY,gyroZ,
//                        imuTempC,magOk,headingDeg,baroOk,baroTempC,presionHpa,altitudM"
// ---------------------------------------------------------------------------
void mostrarTelemetria(char *paquete, int rssi, float snr)
{
  char *campo = strtok(paquete, ",");
  if (campo == NULL || strcmp(campo, "TLM") != 0)
  {
    Serial.print("LoRa RX (paquete no reconocido): ");
    Serial.println(paquete);
    return;
  }

  unsigned long sample = strtoul(strtok(NULL, ","), NULL, 10);
  int imuOk = atoi(strtok(NULL, ","));
  float accX = atof(strtok(NULL, ","));
  float accY = atof(strtok(NULL, ","));
  float accZ = atof(strtok(NULL, ","));
  float gyroX = atof(strtok(NULL, ","));
  float gyroY = atof(strtok(NULL, ","));
  float gyroZ = atof(strtok(NULL, ","));
  float imuTempC = atof(strtok(NULL, ","));
  int magOk = atoi(strtok(NULL, ","));
  float headingDeg = atof(strtok(NULL, ","));
  int baroOk = atoi(strtok(NULL, ","));
  float baroTempC = atof(strtok(NULL, ","));
  float presionHpa = atof(strtok(NULL, ","));
  float altitudM = atof(strtok(NULL, ","));

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

// ---------------------------------------------------------------------------
// Comandos desde el puerto serial -> LoRa hacia la CPV
// ---------------------------------------------------------------------------
char serialBuffer[32];
uint8_t serialIndex = 0;

void enviarComando(const char *comando)
{
  char paquete[40];
  int len = snprintf(paquete, sizeof(paquete), "CMD:%s", comando);
  if (len <= 0)
  {
    return;
  }

  loraSend((uint8_t *)paquete, (uint8_t)len);
  Serial.print("Comando enviado a la CPV: ");
  Serial.println(comando);

  loraStartReceive(); // Volver a modo escucha para no perder telemetria
}

void revisarComandosSerial()
{
  while (Serial.available())
  {
    char c = Serial.read();

    if (c == '\n' || c == '\r')
    {
      if (serialIndex > 0)
      {
        serialBuffer[serialIndex] = '\0';
        enviarComando(serialBuffer);
        serialIndex = 0;
      }
    }
    else if (serialIndex < sizeof(serialBuffer) - 1)
    {
      serialBuffer[serialIndex++] = c;
    }
  }
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);

  if (!loraInit())
  {
    Serial.println("Error: no se detecto el modulo LoRa");
    while (true)
      ;
  }

  loraStartReceive();

  Serial.println("Estacion Terrena lista (sin librerias).");
  Serial.println("Escriba ARMAR o ACTIVAR en el Monitor Serial y presione enter para enviar el comando.");
}

void loop()
{
  revisarComandosSerial();

  uint8_t rxBuffer[128];
  int len = loraReceivePacket(rxBuffer, sizeof(rxBuffer) - 1);
  if (len > 0)
  {
    rxBuffer[len] = '\0';
    int rssi = loraPacketRssi();
    float snr = loraPacketSnr();
    mostrarTelemetria((char *)rxBuffer, rssi, snr);
  }
}
