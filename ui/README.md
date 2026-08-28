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

Los botones `ARMAR` y `ACTIVAR` escriben el comando por serial hacia la estacion terrena. La estacion lo encapsula como `CMD:<comando>` y lo manda a la CPV por LoRa.

## Notas

- Web Serial funciona en Chrome o Edge desde `localhost`.
- La UI tambien puede leer paquetes crudos `TLM,...` y la salida serial legible que ya generaba la estacion.
- `Roll` y `Pitch` se estiman desde el acelerometro. `Yaw` usa `headingDeg`, que viene del magnetometro de la CPV.
