#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_HMC5883_U.h>
#include <Adafruit_BMP085.h>

// GY-87 (MPU6050 + HMC5883L + BMP180) - Telemetria por puerto serial usando librerias
//
// Librerias necesarias (Arduino IDE > Herramientas > Administrar bibliotecas):
//  - Adafruit MPU6050
//  - Adafruit Unified Sensor   (dependencia de las Adafruit_*)
//  - Adafruit HMC5883 Unified
//  - Adafruit BMP085 Library   (compatible con el BMP180)
//
// El HMC5883L esta colgado del bus AUX del MPU6050. Para poder leerlo
// directamente con Adafruit_HMC5883_U hay que activar el modo bypass
// del MPU6050 (igual que en la version sin librerias).

// Pines I2C (ESP32 por defecto)
#define I2C_SDA 21
#define I2C_SCL 22

// Registros del MPU6050 usados solo para activar el bypass
#define MPU6050_ADDR 0x68
#define MPU6050_REG_USER_CTRL 0x6A
#define MPU6050_REG_INT_PIN_CFG 0x37

const float DECLINATION_RAD = 0.0f; // Declinacion magnetica local (ajustar segun ubicacion)
const int32_t SEA_LEVEL_PRESSURE_PA = 101325;

Adafruit_MPU6050 mpu;
Adafruit_HMC5883_Unified mag = Adafruit_HMC5883_Unified(12345);
Adafruit_BMP085 bmp;

uint32_t sampleCount = 0;

void enableMpu6050Bypass()
{
  // Desactiva el modo maestro I2C interno del MPU6050
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU6050_REG_USER_CTRL);
  Wire.write(0x00);
  Wire.endTransmission();

  // Activa I2C_BYPASS_EN (bit 1) para exponer el bus auxiliar (HMC5883L)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU6050_REG_INT_PIN_CFG);
  Wire.write(0x02);
  Wire.endTransmission();
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!mpu.begin())
  {
    Serial.println("Error: no se detecto el MPU6050");
    while (true)
      ;
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  enableMpu6050Bypass();

  if (!mag.begin())
  {
    Serial.println("Error: no se detecto el HMC5883L (revisar bypass)");
    while (true)
      ;
  }

  if (!bmp.begin())
  {
    Serial.println("Error: no se detecto el BMP180");
    while (true)
      ;
  }

  Serial.println("GY-87 listo (con librerias): MPU6050 + HMC5883L + BMP180");
}

void loop()
{
  sampleCount++;

  // ---- MPU6050: acelerometro + giroscopio ----
  sensors_event_t accEvent, gyroEvent, tempEvent;
  mpu.getEvent(&accEvent, &gyroEvent, &tempEvent);

  // ---- HMC5883L: magnetometro ----
  sensors_event_t magEvent;
  mag.getEvent(&magEvent);

  float headingRad = atan2f(magEvent.magnetic.y, magEvent.magnetic.x) + DECLINATION_RAD;
  if (headingRad < 0) headingRad += 2.0f * PI;
  if (headingRad > 2.0f * PI) headingRad -= 2.0f * PI;
  float headingDeg = headingRad * 180.0f / PI;

  // ---- BMP180: barometro ----
  float baroTempC = bmp.readTemperature();
  int32_t pressurePa = bmp.readPressure();
  float altitudeM = bmp.readAltitude(SEA_LEVEL_PRESSURE_PA);

  // ---- Telemetria por Serial ----
  Serial.printf("#%lu ------------------------------------\n", (unsigned long)sampleCount);

  Serial.printf("IMU  | Acc[m/s2]: X:%.2f Y:%.2f Z:%.2f | Gyro[rad/s]: X:%.2f Y:%.2f Z:%.2f | Temp: %.1f C\n",
                accEvent.acceleration.x, accEvent.acceleration.y, accEvent.acceleration.z,
                gyroEvent.gyro.x, gyroEvent.gyro.y, gyroEvent.gyro.z,
                tempEvent.temperature);

  Serial.printf("MAG  | X:%.1f Y:%.1f Z:%.1f (uT) | Heading: %.1f deg\n",
                magEvent.magnetic.x, magEvent.magnetic.y, magEvent.magnetic.z, headingDeg);

  Serial.printf("BARO | Temp: %.1f C | Presion: %.1f hPa | Altitud: %.1f m\n",
                baroTempC, pressurePa / 100.0f, altitudeM);

  delay(500);
}
