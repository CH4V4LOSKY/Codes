# UI Telemetria CPV LoRa

Interfaz local para visualizar la telemetria enviada por la CPV y recibida por la Estacion Terrena con la libreria `LoRa.h`.

## Uso

1. Carga `CPV_LoRa_Telemetria_Lib/CPV_LoRa_Telemetria_Lib.ino` en la computadora de vuelo.
2. Carga `EstacionTerrena_LoRa_Lib/EstacionTerrena_LoRa_Lib.ino` en la estacion terrena.
3. Abre esta carpeta desde un servidor local y entra a `http://localhost:4173`.
4. Pulsa `Conectar`, selecciona el puerto USB de la estacion terrena y usa `115200` baudios.

La estacion terrena imprime una linea serial para la UI con este formato:

```text
UI_TLM,TLM,sample,imuOk,accX,accY,accZ,gyroX,gyroY,gyroZ,imuTempC,magOk,headingDeg,baroOk,baroTempC,presionHpa,altitudM,rssi,snr
```

Los botones `ARMAR` y `ACTIVAR` escriben el comando por serial hacia la estacion terrena. La estacion lo encapsula como `CMD:<seq>:<comando>` y lo manda al receptor por LoRa. El receptor responde `ACK:<seq>:<comando>`.

## Prueba del servo con code_Fredy

Carga `code_Fredy/code_Fredy.ino` en el ESP32 receptor e instala las bibliotecas ESP32Servo y LoRa (Sandeep Mistry). Conserva el programa de la estacion terrena y conecta la UI al puerto USB de esa estacion a 115200 baudios.

- Servo: senal en D14/GPIO14, fuente apropiada para el servo y GND comun con el ESP32.
- LoRa del receptor: SCK 18, MISO 19, MOSI 23, NSS 5, RESET **27**, DIO0 2. Mueve RESET de GPIO14 a GPIO27 para liberar el pin del servo. La estacion conserva RESET en GPIO14.
- `ARMAR` solicita **0 grados**; `ACTIVAR` solicita **90 grados**. El receptor inicia en 0 grados.
- La consola muestra la confirmacion de la estacion cuando recibe el ACK. Esto confirma la orden al servo, no su posicion fisica medida.
- Este receptor solo controla el servo: no transmite telemetria de sensores a las graficas.

## Notas

- Web Serial funciona en Chrome o Edge desde `localhost`.
- La UI tambien puede leer paquetes crudos `TLM,...` y la salida serial legible que ya generaba la estacion.
- `Roll` y `Pitch` se estiman desde el acelerometro. `Yaw` usa `headingDeg`, que viene del magnetometro de la CPV.
