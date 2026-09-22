# CPV: telemetría continua y apertura automática/manual

## Archivos que se cargan

- **CPV:** `CPV_Paracaidas_LoRa_RX.ino`.
- **Estación terrena:** `EstacionTerrena/EstacionTerrena.ino`, dentro de esta misma carpeta. Abrirlo como sketch independiente; no cargarlo en la CPV.
- **Interfaz:** `Codes/ui`, conectada por USB a la estación a **115200 baudios**.

Ambos sketches compilan para **ESP32 Dev Module** (`esp32:esp32:esp32`, ESP32 clásico de dos núcleos), con LoRa de Sandeep Mistry instalada en `Codes/libraries/LoRa`. Mantener la estructura de carpetas: la estación comparte `../RadioProtocol.h` con la CPV.

## Uso

1. Cargar cada sketch en su ESP32 correspondiente.
2. **Encender la CPV.** No inicia una calibración por sí sola. La primera presión válida establece una referencia básica y activa la vigilancia barométrica; la telemetría empieza inmediatamente y los campos no disponibles se muestran como `--`.
3. Ejecutar `node ui/server.mjs` desde `Codes`, abrir http://127.0.0.1:4173 en Chrome o Edge, pulsar **Conectar** y elegir el USB de la estación.
4. Pulsar **ARMAR**. Solo cierra el mecanismo durante 1 s. Esperar a que la UI muestre **ARMADO**; no cambia la referencia, no borra máximos y no solicita calibración.
5. Pulsar **CALIBRAR** con la CPV inmóvil. Recoge 100 lecturas filtradas durante ~2 s para promediar presión, vertical y sesgo de giro. El ACK confirma la solicitud; esperar **CALIBRACIÓN COMPLETA**, no confundirlo con el fin del procedimiento.
6. La UI indica **LISTO PARA VUELO** cuando la CPV reporta armado terminado, calibración manual completa, IMU/barómetro/orientación válidos, sin maniobra, calibración pendiente, vuelo ni apertura. La preparación es informativa: **no habilita ni bloquea la detección automática**.
7. **ACTIVAR** recorre UI → USB → estación → LoRa → CPV. Está disponible sin calibración, armado, telemetría recibida o detección de ascenso. Interrumpe la calibración si estaba en curso.
8. La CPV transmite durante preparación, vuelo, pulso del motor y descenso. El paracaídas pasa de rojo a verde al recibir su estado de activación automática o manual y permanece verde después del pulso. Confirma la orden del firmware, no una posición mecánica medida.

## Calibración manual separada

`FlightPreparation.h` separa el procedimiento manual (`ManualCalibration`) del seguimiento permanente (`FlightMonitor`). CALIBRAR puede mejorar la referencia antes del vuelo; no es un permiso para volar y no acciona el motor. ARMAR tampoco decide si el detector debe ejecutarse.

- Antes de calibrar, la primera lectura válida de presión es el origen de altura. Se emplean la velocidad y el respaldo por descenso barométricos existentes. No se inventa una orientación desde la aceleración de vuelo; la predicción inercial necesita una calibración manual válida.
- Una nueva calibración conserva la referencia anterior mientras recoge datos. Una mediana de tres lecturas por componente elimina picos aislados. Sobre al menos 100 muestras y ~2 s se comprueba la dispersión, en vez de exigir que cada lectura cruda esté cerca del valor ideal. El filtro solo se aplica a la calibración.
- Se admite una magnitud media del acelerómetro entre **8 y 12 m/s²**, compatible con la corrección de sesgo existente, y giro previo a corregir de hasta **10 °/s**. La dispersión RMS vectorial máxima es **0.65 m/s²** en aceleración y **0.035 rad/s (~2 °/s)** en giro. Estos límites distinguen un sesgo constante de variaciones por movimiento; no sustituyen la condición de mantener la CPV inmóvil.
- El movimiento sostenido, una dispersión excesiva, la saturación, lecturas inválidas o huecos de más de 100 ms reinician la ventana. Si no completa el reposo válido en **15 s**, se cancela y espera una nueva orden CALIBRAR; no reintenta automáticamente. Un pico aislado no borra todo el avance.
- La detección de vuelo se evalúa **antes** de aplicar una nueva referencia. Si detecta vuelo, se ordena apertura o se mueve el cierre, se cancela la calibración sin reiniciar el estado ni los máximos del vuelo.
- Solo una calibración completada en tierra actualiza el origen y reinicia los máximos. Una calibración fallida conserva la anterior. ARMAR y CALIBRAR se rechazan cuando el mecanismo/preparación están ocupados o se ha detectado vuelo/apertura; ACTIVAR mantiene prioridad.
- Sin presión válida no se puede actualizar la detección barométrica. La recepción de ACTIVAR y el control del motor continúan independientes de los sensores.

## Reinicio remoto de la CPV

El botón **Reiniciar CPV** envía `REINICIAR` por USB a la estación, que lo transmite por LoRa con el identificador del arranque actual. La estación necesita haber recibido telemetría para conocer ese destino.

- La CPV acepta la orden, la anuncia en telemetría y ejecuta `esp_restart()` después de terminar cualquier pulso del motor, con ambas salidas LOW. No corta el pulso de apertura de 5 s.
- Una nueva orden de apertura manual o automática cancela un reinicio pendiente; ACTIVAR conserva prioridad.
- Los reintentos están ligados al arranque anterior y no pueden reiniciar otra vez la CPV recién arrancada. No se escribe memoria flash para esta protección.
- El ACK significa **orden aceptada**. La estación informa **REINICIADO** al recibir telemetría con un identificador de arranque diferente, incluso si se perdió el ACK. Si no lo recibe en 15 s muestra **SIN_REINICIO**.
- El nuevo arranque vuelve al estado lógico inicial: sin armado confirmado, sin calibración manual, vuelo sin detectar y máximos anteriores borrados. Se obtiene otra referencia básica de presión y la detección vuelve a vigilar. La UI vuelve a rojo cuando recibe ese nuevo estado, no al pulsar el botón.
- **No cierra físicamente el mecanismo.** Para preparar otro vuelo hay que volver a ARMAR y CALIBRAR. El reinicio borra el estado de vuelo aunque ya se hubiera detectado vuelo; debe solicitarse cuando se quiera terminar esa sesión de la CPV.

El reinicio se controla por separado en `RestartControl.h`; no se agregan calibraciones automáticas.

## Lógica de vuelo y motor conservada

`FlightLogic.h` y `Sensors.h` permanecen sin cambios. En `RecoveryControl.h` se agregan los nombres CALIBRAR y REINICIAR; los métodos del motor no cambian. Se conservan los filtros, criterios de lanzamiento/apogeo/descenso, cierre de **1 s**, apertura de **5 s** y **20 ms** de motor apagado al invertir desde cierre.

El automático sigue reconociendo altura ≥3 m y velocidad ≥3 m/s. Con orientación/aceleración válidas anticipa la apertura cuando estima ≤2 s al apogeo. Sin calibración manual, usa el respaldo existente: Vr < −0.5 m/s y caída ≥0.5 m desde el máximo durante 0.15 s. ARMAR sigue bloqueado en vuelo, después de una apertura y mientras el mecanismo está ocupado.

La tarea de sensores continúa estimación y telemetría después de la apertura, incluso si no hubo calibración manual. No vuelve a solicitar una apertura automática después de desplegar ni permite recalibrar un mecanismo ya desplegado.

## Arquitectura de radio

- Sensores en núcleo 0; LoRa y motor exclusivamente en la tarea de núcleo 1.
- Una cola de longitud 1 comparte la muestra más reciente, sin bloquear la radio.
- Telemetría binaria de **100 bytes cada 500 ms (2 Hz)**, con secuencia, identificador de arranque, estado de sensores, estado del paracaídas, máximos y confirmación del último comando.
- SX127x es **half duplex**: alterna TX y RX. Una trama ocupa aproximadamente 174 ms con esta configuración; el resto del periodo queda para recepción. No transmite y recibe físicamente a la vez.
- Transmisión asíncrona; ni el envío ni los registros USB esperan dentro de la tarea del motor. Se retorna a recepción continua al completar TX; un límite de 500 ms recupera el modo RX si TX queda atascado.
- La estación recibe y convierte las tramas a líneas `UI_TLM2`. Su tarea de radio tiene colas separadas del USB.
- La estación espera 20 ms después de recibir telemetría para transmitir una orden. Si no hay telemetría, también puede transmitir. Repite hasta **8 intentos**, separados por **700–900 ms**, hasta recibir confirmación.
- Cada intención lleva sesión y secuencia. La CPV guarda los resultados de las últimas 16 órdenes; sus retransmisiones no reinician un pulso ni repiten la calibración. No hay reintentos ilimitados.
- ACTIVAR sustituye un ARMAR o CALIBRAR pendiente. Las órdenes de preparación no se sustituyen entre sí: esperar a terminar un paso antes de solicitar el siguiente. Clics repetidos de la misma orden pendiente conservan su identidad. Después de confirmar/agotarse los intentos, un nuevo ACTIVAR deliberado puede repetir el pulso, como antes.
- La confirmación viaja repetida en telemetría. La UI distingue pendiente, transmitido, aceptado/rechazado y sin confirmación.
- El estado de apertura y su origen se incluyen en **cada** trama; perder la primera no impide actualizar la UI después.
- Los máximos se acumulan a ritmo de sensores (~50 Hz), no solo en los paquetes recibidos. Una muestra de más de 300 ms se marca sin datos actuales, conservando los máximos y el estado del motor.
- Si falla LoRa al iniciar, continúa el reintento de inicialización mientras el motor está apagado; los sensores y el automático siguen independientes.

RF de ambos: **433 MHz, SF7, BW125 kHz, CR4/5, sync 0x12, preámbulo 8, CRC habilitado, IQ normal, cabecera explícita**. Se conservan los parámetros anteriores y se activa CRC en las transmisiones nuevas.

La CPV también acepta los paquetes de texto exactos `ARMAR` / `ACTIVAR` de la estación anterior; esos paquetes no tienen identidad y por tanto no se deduplican ni tienen ACK identificado. La estación nueva utiliza el protocolo binario CPV2/CMD2 y está destinada a esta CPV, no al receptor antiguo de Freddy. Los sketches antiguos permanecen sin cambios.

## Datos y memoria de vuelo

- Acelerómetros X/Y/Z en g; giroscopios en °/s; presión en hPa.
- Altura relativa en m, velocidad vertical en m/s y aceleración vertical neta filtrada en m/s², procedentes de la estimación de la CPV. La UI no deriva la velocidad a partir de los tiempos de llegada USB.
- **Velocidad máxima:** máximo de |velocidad vertical|, incluyendo descenso.
- **Aceleración máxima:** máximo de |aceleración vertical neta filtrada|, excluyendo muestras inválidas/saturadas.
- **Altura máxima:** máximo respecto de la referencia de calibración.
- Máximos en RAM de la CPV hasta reiniciar o **completar** una calibración manual en tierra. ARMAR, solicitar CALIBRAR o fallar una calibración no los borra. Sobreviven a pérdidas de paquetes y reconexiones de la UI porque se retransmiten. No se guardan al apagar la CPV.
- Temperaturas y rumbo quedan en `--`: el controlador actual no los entrega. La inclinación aparente de la UI deriva del acelerómetro; no sustituye la proyección vertical que usa la lógica de vuelo.

## Conexiones

| Elemento | GPIO ESP32 |
|---|---|
| I²C SDA / SCL (CPV) | 21 / 22 |
| LoRa SCK / MISO / MOSI (ambas placas) | 18 / 19 / 23 |
| LoRa NSS / RESET / DIO0 (ambas placas) | 5 / 14 / 2 |
| Entrada A / B del controlador de motor (CPV) | 25 / 26 |

Apertura: GPIO25 HIGH / GPIO26 LOW. Cierre: GPIO25 LOW / GPIO26 HIGH. Al terminar: ambos LOW. Se utiliza el controlador de motor y GND común existentes.

## Protocolo USB de la estación

`UI_TLM2,boot,seq,ms,flags,origen,estadoVuelo,epoca,ax,ay,az,gx,gy,gz,presion,altura,velocidad,aceleracion,alturaMax,velMax,accMax,rssi,snr,magic`

`magic` es el entero 0x32565043; origen: 0 sin activar, 1 automático, 2 manual. Banderas: 1 IMU válida, 2 barómetro válido, 4 calibración manual completa, 8 orientación válida, 16 activado, 32 vuelo detectado, 64 motor abriendo, 128 cerrando, 256 aceleración válida, 512 calibración solicitada/en curso, 1024 armado terminado, 2048 referencia básica disponible, 4096 calibración cancelada/fallida, 8192 reinicio pendiente. `nan` representa un dato no disponible. La época cambia solo al completar una calibración. La telemetría sigue siendo de 100 bytes.

`UI_CMD,etapa,sesion,secuencia,ARMAR|CALIBRAR|ACTIVAR|REINICIAR,resultado`

Etapas: PENDIENTE, TX, ACK, SIN_CONFIRMACION, REINICIADO, SIN_REINICIO, SIN_DESTINO y CANCELADO. Resultado ACK: 1 solicitud aceptada, 2 ignorada por el estado de la CPV. Códigos binarios: ARMAR 1, ACTIVAR 2, CALIBRAR 3, REINICIAR 4. Los comandos nuevos ocupan **20 bytes**: magic, sesión, secuencia, comando y arranque destinatario, todos uint32. Se admiten también los antiguos 16 bytes para comandos que no sean reinicio. REINICIAR exige destinatario válido; un texto LoRa sin identificación no reinicia la CPV. Los paquetes se definen en `RadioProtocol.h` para ESP32 little endian. Actualizar **ambos ESP32 y recargar la UI**.

## Sonidos de la estación

Se reproducen en los altavoces de la computadora mediante `ui/sounds.js`, independientemente de las tareas de radio/motor. No requieren buzzer ni otro GPIO. La UI permite activar/silenciar, ajustar volumen y probar sonido sin enviar órdenes. La activación del paracaídas tiene un aviso distintivo, una vez por arranque y solo después de recibir el estado de la CPV, tanto automático como manual. Hay avisos de armado terminado, calibración/preparación completa, nuevo arranque y pérdida de telemetría. Un fallo de audio no impide comandos ni recepción.

## Verificación

Pruebas de reinicio añadidas: telemetría/ACK antes del reset, espera del pulso completo de 5 s, rechazo del mismo reinicio tras un nuevo arranque, prioridad de apertura, rechazo sin destino conocido y confirmación por nuevo arranque tras perder el ACK. Pruebas de audio: patrón de activación automático/manual, no repetir con cada paquete, silencio, volumen y ausencia de soporte de audio.

Pruebas añadidas para preparación manual: reposo con sesgo, ruido y picos aislados que antes impedían calibrar; rechazo de movimiento, dispersión excesiva, datos inválidos y huecos; no calibrar automáticamente en reposo; aceptar y completar CALIBRAR; cancelar por falta de reposo en 15 s; conservar la referencia anterior; detectar lanzamiento durante la calibración sin reiniciar el vuelo; recibir el comando completo UI/USB/LoRa/ACK; no calibrar por ARMAR; rechazar calibración durante cierre/vuelo y mostrar LISTO solo con estados confirmados y telemetría reciente. Sin armado, sin IMU y sin calibración manual, la trayectoria de prueba dispara el mismo respaldo barométrico a 15.24 s.

Compilación de ambos sketches: ESP32 core 3.3.7 y LoRa 0.8.0. Pruebas en PC de la lógica existente y de las tareas reales de ambos sketches con sustitutos de radio/USB/I²C: telemetría cada 500 ms después de apertura, inversión de 20 ms, apertura de 5 s, duplicados durante/después del pulso, reintento deliberado, apertura automática, sensores ausentes, ACK perdido y retransmitido. Reproducción de la trayectoria existente: automático a 12.78 s y respaldo barométrico a 15.24 s, sin cambios.

```powershell
& Proyecto_HC2026_RW/CPV_Paracaidas_LoRa_RX/tests/run.cmd
& ui/tests/run.cmd
& tools/arduino-local/arduino-local.exe compile --fqbn esp32:esp32:esp32 --build-path build/cpv-lora-v2 Proyecto_HC2026_RW/CPV_Paracaidas_LoRa_RX
& tools/arduino-local/arduino-local.exe compile --fqbn esp32:esp32:esp32 --build-path build/cpv-ground-v2 Proyecto_HC2026_RW/CPV_Paracaidas_LoRa_RX/EstacionTerrena
```

**No se cargó firmware ni se realizó una prueba física de radio o apertura en esta tarea.** Las pruebas con sustitutos verifican el comportamiento del código, no el enlace RF ni el mecanismo.
