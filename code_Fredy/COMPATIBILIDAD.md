# Enlace simple: estacion ESP32 -> Nano Fredy

La estacion envia una sola vez el texto ARMAR o ACTIVAR por LoRa.
No hay numeros de secuencia, confirmaciones ni reintentos.
Esta version de la estacion solo transmite comandos; no recibe telemetria.

- ARMAR: el Nano solicita 0 grados.
- ACTIVAR: el Nano solicita 90 grados y mantiene esa posicion.
- Al encender o reiniciar el Nano: solicita 0 grados.
- Otros paquetes se ignoran.

Ambos usan LoRa de Sandeep Mistry: 433 MHz, SF7, 125 kHz, CR 4/5,
sync word 0x12, preambulo 8, cabecera explicita, CRC desactivado e IQ normal.

| Conexion | Nano clasico ATmega328P | ESP32 clasico |
|---|---|---|
| SCK | D13 | GPIO18 |
| MISO | D12 | GPIO19 |
| MOSI | D11 | GPIO23 |
| NSS | D10 | GPIO5 |
| RESET | D9 | GPIO14 |
| DIO0 | D2 | GPIO2 |
| Servo | D5 | No aplica |

El RA-02 requiere fuente regulada de 3.3 V y adaptacion de nivel desde
las salidas de 5 V del Nano (SCK, MOSI, NSS, RESET). Alimentar el servo
con una fuente apropiada y GND comun. Usar antenas adecuadas.

Cargar ambos programas nuevos: no usan el antiguo formato CMD:secuencia:comando.
Seleccionar Arduino Nano/ATmega328P para Fredy y ESP32 para la estacion.

Prueba: abrir el monitor serial del ESP32 a 115200 con nueva linea o retorno
de carro. Enviar ARMAR y luego ACTIVAR; comprobar 0 y 90 grados respectivamente.
El mensaje Enviado solo indica transmision, no recepcion ni movimiento.
Si se pierde un paquete, es necesario volver a enviar el comando manualmente.

Revision estatica realizada; compilacion y prueba fisica pendientes.
