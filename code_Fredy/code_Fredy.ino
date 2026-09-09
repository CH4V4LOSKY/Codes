/*
  code_Fredy: receptor LoRa para Arduino Nano clasico (ATmega328P).
  Bibliotecas Arduino: Servo y LoRa (Sandeep Mistry).
  UI -> USB -> EstacionTerrena_LoRa_Lib -> LoRa -> code_Fredy.
  ARMAR = 0 grados; ACTIVAR = 90 grados. Inicio: 0 grados.

  Cableado del receptor Nano:
  Servo: senal D5, alimentacion externa apropiada y GND comun al Nano.
  LoRa: SCK D13, MISO D12, MOSI D11, NSS D10, RESET D9, DIO0 D2.
  RA-02: alimentacion regulada de 3.3 V y adaptacion de nivel de las
  salidas de 5 V del Nano (SCK, MOSI, NSS, RESET) hacia el modulo.

  Pulsos predeterminados de Servo.h: ajustar segun la ficha del servo para calibrar
  los angulos reales. Este programa no mide la posicion fisica del servo.
  Monitor serie: 115200 baudios.
*/
#include <SPI.h>
#include <LoRa.h>
#include <Servo.h>

// ===============================
// PINES ARDUINO NANO
// ===============================

// Servo
constexpr int SERVO_PIN = 5;

// LoRa SX1278 / RA-02
constexpr int LORA_NSS  = 10;
constexpr int LORA_RST  = 9;
constexpr int LORA_DIO0 = 2;

// SPI del Nano:
// SCK  = D13
// MISO = D12
// MOSI = D11
// NSS  = D10

constexpr long LORA_FREQUENCY = 433000000;

// ===============================
// SERVO
// ===============================

Servo servo;


// ===============================
// PROCESAR COMANDO LORA
// ===============================

void procesarComando(const String &paquete) {

  // Formato esperado:
  // CMD:<seq>:<comando>
  //
  // Ejemplos:
  // CMD:1:ARMAR
  // CMD:2:ACTIVAR

  if (!paquete.startsWith("CMD:")) {
    return;
  }

  int separador = paquete.indexOf(':', 4);

  if (separador <= 4) {
    return;
  }

  String secuencia = paquete.substring(4, separador);

  // Verificar que la secuencia contenga solamente números
  for (unsigned int i = 0; i < secuencia.length(); ++i) {
    if (secuencia[i] < '0' || secuencia[i] > '9') {
      return;
    }
  }

  String comando = paquete.substring(separador + 1);

  int angulo;

  if (comando == "ARMAR") {

    angulo = 0;

  }
  else if (comando == "ACTIVAR") {

    angulo = 90;

  }
  else {

    Serial.print("Comando desconocido: ");
    Serial.println(comando);

    return;
  }

  // Mover servo
  servo.write(angulo);

  Serial.print(comando);
  Serial.print(" -> posicion solicitada: ");
  Serial.println(angulo);


  // ===============================
  // ENVIAR ACK A ESTACION TERRENA
  // ===============================

  LoRa.beginPacket();

  LoRa.print("ACK:");
  LoRa.print(secuencia);
  LoRa.print(":");
  LoRa.print(comando);

  LoRa.endPacket();
  LoRa.parsePacket(); // volver inmediatamente a RX con el mismo modo de sondeo
}


// ===============================
// SETUP
// ===============================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println("==============================");
  Serial.println("INICIANDO SISTEMA");
  Serial.println("==============================");


  // ===============================
  // INICIALIZAR SERVO
  // ===============================

  servo.write(0); // preparar el pulso inicial antes de habilitar la salida
  servo.attach(SERVO_PIN);

  // IMPORTANTE:
  // Siempre iniciar bloqueado
  servo.write(0);

  delay(1000);

  Serial.println("Servo iniciado en 0 grados.");


  // ===============================
  // INICIALIZAR LORA
  // ===============================

  // NSS / CS
  pinMode(LORA_NSS, OUTPUT);
  digitalWrite(LORA_NSS, HIGH);

  SPI.begin();

  LoRa.setPins(
    LORA_NSS,
    LORA_RST,
    LORA_DIO0
  );


  if (!LoRa.begin(LORA_FREQUENCY)) {

    Serial.println("ERROR: No se pudo iniciar LoRa.");
    Serial.println("Revisar conexiones.");

    while (true) {
      delay(1000);
    }
  }


  // ===============================
  // CONFIGURACION LORA
  // ===============================

  LoRa.setSpreadingFactor(7);

  LoRa.setSignalBandwidth(125E3);

  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.setPreambleLength(8);
  LoRa.disableCrc(); // conserva compatibilidad con la telemetria CPV existente
  LoRa.disableInvertIQ();


  Serial.println("LoRa iniciado correctamente.");
  Serial.println();
  Serial.println("Sistema listo.");
  Serial.println("ARMAR   = Servo 0 grados");
  Serial.println("ACTIVAR = Servo 90 grados");
  Serial.println("==============================");
}


// ===============================
// LOOP
// ===============================

void loop() {

  int packetSize = LoRa.parsePacket();

  if (packetSize == 0) {
    return;
  }


  String paquete = "";

  while (LoRa.available()) {

    paquete += (char)LoRa.read();

  }


  Serial.print("Paquete recibido: ");
  Serial.println(paquete);


  procesarComando(paquete);
}
