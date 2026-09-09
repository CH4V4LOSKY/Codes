# Nano Fredy y estacion ESP32

Revision estatica: ambos usan LoRa de Sandeep Mistry, 433 MHz, SF7,
125 kHz, CR 4/5, sync word 0x12, preambulo de 8 simbolos, cabecera
explicita, IQ normal y CRC desactivado. Se conserva el perfil de la CPV.

| Conexion | Nano clasico ATmega328P | Estacion ESP32 clasico |
|---|---|---|
| LoRa SCK | D13 | GPIO18 |
| LoRa MISO | D12 | GPIO19 |
| LoRa MOSI | D11 | GPIO23 |
| LoRa NSS | D10 | GPIO5 |
| LoRa RESET | D9 | GPIO14 |
| LoRa DIO0 | D2 | GPIO2 |
| Servo | D5 | No aplica |

El Nano necesita Servo y LoRa; el ESP32 necesita LoRa. Seleccionar Arduino
Nano/ATmega328P al cargar Fredy y la placa ESP32 correspondiente al cargar
la estacion. La configuracion actual de VS Code apunta a la estacion ESP32.

El RA-02 requiere 3.3 V regulados con corriente suficiente para transmitir
(la biblioteca recomienda al menos 120 mA), GND comun y adaptacion de nivel
en SCK, MOSI, NSS y RESET desde el Nano de 5 V. No conectar estas salidas
directamente al RA-02. Alimentar el servo con una fuente apropiada y GND
comun. Conectar antenas de la banda adecuada antes de transmitir.

## Prueba pendiente en hardware

1. Probar el servo sin carga ni mecanismo conectado. Encender ambos equipos
   y verificar que LoRa inicia sin errores; Fredy solicita 0 grados.
2. Abrir el serial del ESP32 a 115200 con terminacion de linea y enviar ARMAR.
   La estacion transmite CMD:1:ARMAR; el Nano solicita 0 grados y responde
   ACK:1:ARMAR. La estacion debe mostrar la confirmacion y parar reintentos.
3. Enviar ACTIVAR: CMD:2:ACTIVAR debe producir 90 grados y ACK:2:ACTIVAR.
4. Desconectar la alimentacion del Nano y enviar ARMAR: la estacion debe
   reintentar cada 200 ms aproximadamente y reportar falta de ACK tras 3 s.

El ACK indica que se solicito la posicion, no que se midio el movimiento.
Fredy no genera telemetria TLM: esos datos requieren la CPV con sensores.
Evitar otro receptor que responda a los mismos comandos durante esta prueba:
el protocolo actual no incluye direccion de dispositivo.

No se compilo ni se probo por radio en esta revision: no se encontro
arduino-cli en PATH ni se dispuso de una prueba con las placas.

Referencias: https://github.com/sandeepmistry/arduino-LoRa/blob/master/API.md
y https://github.com/sandeepmistry/arduino-LoRa#compatible-hardware
