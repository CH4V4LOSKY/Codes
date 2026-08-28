#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <math.h>

// ============================================================================
// Computadora de Vuelo (CPV) - GY-87 + LoRa (con libreria LoRa.h)
// ----------------------------------------------------------------------------
// - Lee la telemetria del GY-87 (MPU6050 + HMC5883L + BMP180), igual que en
//   GY87_Telemetria_ESP32.ino (a registros crudos via Wire).
// - Envia esa telemetria por LoRa a la Estacion Terrena usando la libreria
//   LoRa.h (Sandeep Mistry), en vez del driver a registros crudos.
// - Escucha comandos que llegan por LoRa desde la Estacion Terrena. Cuando
//   llega "ARMAR" o "ACTIVAR" se llama a la funcion correspondiente. Esas
//   funciones se dejan en blanco a proposito: el usuario coloca alli la
//   logica real (armado del sistema / giro del motor).
// ============================================================================

// Pines I2C (ESP32 por defecto)
#define I2C_SDA 21
#define I2C_SCL 22

// ---------------------------------------------------------------------------
// MPU6050
// ---------------------------------------------------------------------------
#define MPU6050_ADDR 0x68
#define MPU6050_REG_PWR_MGMT_1 0x6B
#define MPU6050_REG_USER_CTRL 0x6A
#define MPU6050_REG_INT_PIN_CFG 0x37
#define MPU6050_REG_ACCEL_XOUT_H 0x3B

const float ACCEL_SCALE = 16384.0f; // LSB/g para rango +-2g
const float GYRO_SCALE = 131.0f;    // LSB/(deg/s) para rango +-250 deg/s

// ---------------------------------------------------------------------------
// HMC5883L
// ---------------------------------------------------------------------------
#define HMC5883L_ADDR 0x1E
#define HMC5883L_REG_CONFIG_A 0x00
#define HMC5883L_REG_CONFIG_B 0x01
#define HMC5883L_REG_MODE 0x02
#define HMC5883L_REG_DATA_X_MSB 0x03

const float HMC5883L_SCALE = 1.0f / 1090.0f; // Gauss/LSB con ganancia por defecto
const float DECLINATION_RAD = 0.0f;          // Declinacion magnetica local (ajustar segun ubicacion)

// ---------------------------------------------------------------------------
// BMP180
// ---------------------------------------------------------------------------
#define BMP180_ADDR 0x77
#define BMP180_REG_CALIB_START 0xAA
#define BMP180_REG_CONTROL 0xF4
#define BMP180_REG_RESULT 0xF6
#define BMP180_CMD_READ_TEMP 0x2E
#define BMP180_CMD_READ_PRESSURE 0x34 // OSS = 0 (sin sobremuestreo)
#define BMP180_OSS 0
#define SEA_LEVEL_PRESSURE_PA 101325.0f

struct Bmp180Calib
{
  int16_t AC1, AC2, AC3;
  uint16_t AC4, AC5, AC6;
  int16_t B1, B2;
  int16_t MB, MC, MD;
};

Bmp180Calib bmpCalib;

uint32_t sampleCount = 0;

// ---------------------------------------------------------------------------
// LoRa - pines (deben coincidir con la Estacion Terrena)
// ---------------------------------------------------------------------------
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_NSS 5
#define LORA_RST 14
#define LORA_DIO0 2

#define LORA_FREQUENCY 433E6 // 433 MHz (ajustar si el modulo es de otra banda)

// ---------------------------------------------------------------------------
// Utilidades I2C
// ---------------------------------------------------------------------------
void writeRegister(uint8_t deviceAddr, uint8_t reg, uint8_t value)
{
  Wire.beginTransmission(deviceAddr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

bool readRegisters(uint8_t deviceAddr, uint8_t startReg, uint8_t *buffer, uint8_t count)
{
  Wire.beginTransmission(deviceAddr);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0)
  {
    return false;
  }

  if (Wire.requestFrom((int)deviceAddr, (int)count) != count)
  {
    return false;
  }

  for (uint8_t i = 0; i < count; i++)
  {
    buffer[i] = Wire.read();
  }
  return true;
}

// ---------------------------------------------------------------------------
// MPU6050
// ---------------------------------------------------------------------------
void mpu6050WakeUp()
{
  writeRegister(MPU6050_ADDR, MPU6050_REG_PWR_MGMT_1, 0x00);
}

void mpu6050EnableBypass()
{
  writeRegister(MPU6050_ADDR, MPU6050_REG_USER_CTRL, 0x00);
  writeRegister(MPU6050_ADDR, MPU6050_REG_INT_PIN_CFG, 0x02); // I2C_BYPASS_EN
}

bool mpu6050Read(float &accX, float &accY, float &accZ,
                  float &gyroX, float &gyroY, float &gyroZ,
                  float &tempC)
{
  uint8_t raw[14];
  if (!readRegisters(MPU6050_ADDR, MPU6050_REG_ACCEL_XOUT_H, raw, 14))
  {
    return false;
  }

  int16_t rawAccX = (int16_t)((raw[0] << 8) | raw[1]);
  int16_t rawAccY = (int16_t)((raw[2] << 8) | raw[3]);
  int16_t rawAccZ = (int16_t)((raw[4] << 8) | raw[5]);
  int16_t rawTemp = (int16_t)((raw[6] << 8) | raw[7]);
  int16_t rawGyroX = (int16_t)((raw[8] << 8) | raw[9]);
  int16_t rawGyroY = (int16_t)((raw[10] << 8) | raw[11]);
  int16_t rawGyroZ = (int16_t)((raw[12] << 8) | raw[13]);

  accX = rawAccX / ACCEL_SCALE;
  accY = rawAccY / ACCEL_SCALE;
  accZ = rawAccZ / ACCEL_SCALE;
  gyroX = rawGyroX / GYRO_SCALE;
  gyroY = rawGyroY / GYRO_SCALE;
  gyroZ = rawGyroZ / GYRO_SCALE;
  tempC = (rawTemp / 340.0f) + 36.53f;

  return true;
}

// ---------------------------------------------------------------------------
// HMC5883L
// ---------------------------------------------------------------------------
void hmc5883lInit()
{
  writeRegister(HMC5883L_ADDR, HMC5883L_REG_CONFIG_A, 0x70); // 8 muestras, 15 Hz
  writeRegister(HMC5883L_ADDR, HMC5883L_REG_CONFIG_B, 0x20); // Ganancia por defecto
  writeRegister(HMC5883L_ADDR, HMC5883L_REG_MODE, 0x00);     // Medicion continua
}

bool hmc5883lRead(int16_t &x, int16_t &y, int16_t &z)
{
  uint8_t raw[6];
  if (!readRegisters(HMC5883L_ADDR, HMC5883L_REG_DATA_X_MSB, raw, 6))
  {
    return false;
  }

  // Orden de registros del HMC5883L: X, Z, Y
  x = (int16_t)((raw[0] << 8) | raw[1]);
  z = (int16_t)((raw[2] << 8) | raw[3]);
  y = (int16_t)((raw[4] << 8) | raw[5]);

  return true;
}

// ---------------------------------------------------------------------------
// BMP180
// ---------------------------------------------------------------------------
bool bmp180ReadCalibration()
{
  uint8_t raw[22];
  if (!readRegisters(BMP180_ADDR, BMP180_REG_CALIB_START, raw, 22))
  {
    return false;
  }

  bmpCalib.AC1 = (int16_t)((raw[0] << 8) | raw[1]);
  bmpCalib.AC2 = (int16_t)((raw[2] << 8) | raw[3]);
  bmpCalib.AC3 = (int16_t)((raw[4] << 8) | raw[5]);
  bmpCalib.AC4 = (uint16_t)((raw[6] << 8) | raw[7]);
  bmpCalib.AC5 = (uint16_t)((raw[8] << 8) | raw[9]);
  bmpCalib.AC6 = (uint16_t)((raw[10] << 8) | raw[11]);
  bmpCalib.B1 = (int16_t)((raw[12] << 8) | raw[13]);
  bmpCalib.B2 = (int16_t)((raw[14] << 8) | raw[15]);
  bmpCalib.MB = (int16_t)((raw[16] << 8) | raw[17]);
  bmpCalib.MC = (int16_t)((raw[18] << 8) | raw[19]);
  bmpCalib.MD = (int16_t)((raw[20] << 8) | raw[21]);

  return true;
}

int32_t bmp180ReadRawTemp()
{
  writeRegister(BMP180_ADDR, BMP180_REG_CONTROL, BMP180_CMD_READ_TEMP);
  delay(5);

  uint8_t raw[2];
  readRegisters(BMP180_ADDR, BMP180_REG_RESULT, raw, 2);
  return (int32_t)((raw[0] << 8) | raw[1]);
}

int32_t bmp180ReadRawPressure()
{
  writeRegister(BMP180_ADDR, BMP180_REG_CONTROL, BMP180_CMD_READ_PRESSURE + (BMP180_OSS << 6));
  delay(5); // Suficiente para OSS = 0 (4.5 ms segun datasheet)

  uint8_t raw[3];
  readRegisters(BMP180_ADDR, BMP180_REG_RESULT, raw, 3);
  int32_t up = (int32_t)(((uint32_t)raw[0] << 16) | ((uint32_t)raw[1] << 8) | raw[2]);
  up >>= (8 - BMP180_OSS);
  return up;
}

// Formulas de compensacion segun el datasheet del BMP180
bool bmp180Read(float &temperatureC, float &pressurePa, float &altitudeM)
{
  int32_t ut = bmp180ReadRawTemp();
  int32_t up = bmp180ReadRawPressure();

  int32_t x1 = ((ut - (int32_t)bmpCalib.AC6) * (int32_t)bmpCalib.AC5) >> 15;
  int32_t x2 = ((int32_t)bmpCalib.MC << 11) / (x1 + bmpCalib.MD);
  int32_t b5 = x1 + x2;
  temperatureC = ((b5 + 8) >> 4) / 10.0f;

  int32_t b6 = b5 - 4000;
  x1 = ((int32_t)bmpCalib.B2 * ((b6 * b6) >> 12)) >> 11;
  x2 = ((int32_t)bmpCalib.AC2 * b6) >> 11;
  int32_t x3 = x1 + x2;
  int32_t b3 = ((((int32_t)bmpCalib.AC1 * 4 + x3) << BMP180_OSS) + 2) / 4;

  x1 = ((int32_t)bmpCalib.AC3 * b6) >> 13;
  x2 = ((int32_t)bmpCalib.B1 * ((b6 * b6) >> 12)) >> 16;
  x3 = ((x1 + x2) + 2) >> 2;
  uint32_t b4 = (uint32_t)bmpCalib.AC4 * (uint32_t)(x3 + 32768) >> 15;
  uint32_t b7 = ((uint32_t)up - b3) * (50000 >> BMP180_OSS);

  int32_t p;
  if (b7 < 0x80000000UL)
  {
    p = (b7 * 2) / b4;
  }
  else
  {
    p = (b7 / b4) * 2;
  }

  x1 = (p >> 8) * (p >> 8);
  x1 = (x1 * 3038) >> 16;
  x2 = (-7357 * p) >> 16;
  p = p + ((x1 + x2 + 3791) >> 4);

  pressurePa = (float)p;
  altitudeM = 44330.0f * (1.0f - powf(pressurePa / SEA_LEVEL_PRESSURE_PA, 1.0f / 5.255f));

  return true;
}

// ---------------------------------------------------------------------------
// Comandos desde la Estacion Terrena
// ---------------------------------------------------------------------------

// TODO (usuario): logica real para armar el sistema.
void armarSistema()
{
}

// TODO (usuario): logica real para activar el sistema (giro del motor).
void activarSistema()
{
}

void procesarComandoRecibido(const String &paquete)
{
  const String prefijo = "CMD:";

  if (!paquete.startsWith(prefijo))
  {
    Serial.print("LoRa RX (ignorado, no es comando): ");
    Serial.println(paquete);
    return;
  }

  String comando = paquete.substring(prefijo.length());
  Serial.print("Comando recibido desde la Estacion Terrena: ");
  Serial.println(comando);

  if (comando == "ARMAR")
  {
    armarSistema();
  }
  else if (comando == "ACTIVAR")
  {
    activarSistema();
  }
  else
  {
    Serial.println("Comando desconocido");
  }
}

void revisarComandosLoRa()
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

  procesarComandoRecibido(paquete);
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);

  Wire.begin(I2C_SDA, I2C_SCL);

  mpu6050WakeUp();
  mpu6050EnableBypass();
  hmc5883lInit();

  if (!bmp180ReadCalibration())
  {
    Serial.println("Error: no se pudo leer la calibracion del BMP180");
  }

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY))
  {
    Serial.println("Error: no se detecto el modulo LoRa");
    while (true)
      ;
  }

  LoRa.setTxPower(17);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);

  Serial.println("CPV lista: GY-87 + LoRa (libreria LoRa.h)");
}

void loop()
{
  sampleCount++;

  // ---- MPU6050: acelerometro + giroscopio ----
  float accX, accY, accZ, gyroX, gyroY, gyroZ, imuTempC;
  bool imuOk = mpu6050Read(accX, accY, accZ, gyroX, gyroY, gyroZ, imuTempC);

  // ---- HMC5883L: magnetometro ----
  int16_t magX, magY, magZ;
  bool magOk = hmc5883lRead(magX, magY, magZ);
  float headingDeg = 0.0f;
  if (magOk)
  {
    float gaussX = magX * HMC5883L_SCALE;
    float gaussY = magY * HMC5883L_SCALE;
    float headingRad = atan2f(gaussY, gaussX) + DECLINATION_RAD;
    if (headingRad < 0) headingRad += 2.0f * PI;
    if (headingRad > 2.0f * PI) headingRad -= 2.0f * PI;
    headingDeg = headingRad * 180.0f / PI;
  }

  // ---- BMP180: barometro ----
  float baroTempC, pressurePa, altitudeM;
  bool baroOk = bmp180Read(baroTempC, pressurePa, altitudeM);

  // ---- Empaquetar telemetria y enviar por LoRa ----
  char packet[160];
  int packetLen = snprintf(packet, sizeof(packet),
                            "TLM,%lu,%d,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f,%d,%.1f,%d,%.1f,%.1f,%.1f",
                            (unsigned long)sampleCount,
                            (int)imuOk, accX, accY, accZ, gyroX, gyroY, gyroZ, imuTempC,
                            (int)magOk, headingDeg,
                            (int)baroOk, baroTempC, pressurePa / 100.0f, altitudeM);

  if (packetLen > 0)
  {
    LoRa.beginPacket();
    LoRa.write((uint8_t *)packet, (size_t)packetLen);
    LoRa.endPacket();

    Serial.print("LoRa TX -> ");
    Serial.println(packet);
  }

  // ---- Ventana de escucha por comandos entrantes de la Estacion Terrena ----
  uint32_t listenStart = millis();
  while (millis() - listenStart < 300)
  {
    revisarComandosLoRa();
  }

  delay(200);
}
