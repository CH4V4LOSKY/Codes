# CPV con apertura automática y respaldo LoRa

Abrir `CPV_Paracaidas_LoRa_RX.ino` y compilar para **ESP32 Dev Module** (`esp32:esp32:esp32`, ESP32 clásico de dos núcleos). Instalar/utilizar LoRa de Sandeep Mistry, disponible en `Codes/libraries/LoRa`. No requiere las bibliotecas Adafruit: conserva lecturas por registros I²C del código CPV original.

Se combina el enfoque de sensores de `CPV_LoRa_Telemetria_Lib` con la lógica de `SIM/CPV_Paracaidas_SIM`. Es una carpeta nueva; los sketches de CPV anteriores y `EstacionTerrena_LoRa_Lib` no se modifican.

## Uso

1. Cargar este sketch en la CPV y conservar en la estación su sketch actual `EstacionTerrena_LoRa_Lib.ino`.
2. Al encender, el motor queda apagado y se inicia LoRa antes de la calibración. Mantener inmóvil la CPV durante aproximadamente 2 s de lecturas válidas. Ver **AUTO LISTO** en el monitor USB de la CPV a 115200 baudios. Si faltan sensores, la calibración espera y la recepción de ACTIVAR sigue independiente.
3. El automático se habilita al completar la calibración, sin necesitar un mensaje de la estación. El comando ARMAR sirve para cerrar el mecanismo antes del lanzamiento; después de su pulso de 1 s se requieren otras ~2 s inmóviles para recalibrar.
4. Lanzar solo después de completar esa referencia en reposo. La lógica reconoce ascenso con altura ≥3 m y velocidad vertical ≥3 m/s.
5. La CPV ordena abrir cuando estima que faltan 2 s o menos para el apogeo. El motor gira en apertura **5 s reales**, luego ambos pines quedan LOW.
6. Para el respaldo, escribir **ACTIVAR** en el monitor de la estación y pulsar Enter. Se abre con el mismo pulso de 5 s incluso sin calibración, sensores válidos o detección de ascenso.

No se ha cargado ni probado este nuevo sketch con hardware en esta tarea. Las pruebas de software y compilación no verifican cobertura RF, orientación física, ventilación del barómetro ni apertura mecánica.

## Comandos compatibles con la estación existente

| Paquete LoRa exacto | Acción en la CPV |
|---|---|
| `ARMAR` | GPIO25 LOW / GPIO26 HIGH durante 1 s; recalibra antes de vuelo. |
| `ACTIVAR` | GPIO25 HIGH / GPIO26 LOW durante 5 s; apertura prioritaria. |

La estación envía estos textos directos, **sin** `CMD:<seq>:`. La CPV compara el tamaño y contenido exactos. No se aceptan paquetes desconocidos o truncados. La estación ya elimina espacios exteriores y convierte a mayúsculas antes de enviarlos.

ARMAR se ignora después de reconocer ascenso, después de una orden de apertura, o mientras el motor está ocupado. ACTIVAR puede interrumpir el armado: apaga ambos pines 20 ms antes de invertir el sentido. Las órdenes ACTIVAR que llegan durante apertura no reinician ni extienden el pulso. Después de terminarlo, una nueva orden ACTIVAR permite repetir otros 5 s como intento manual. La apertura automática es de una sola ejecución; para un nuevo vuelo completo se reinicia la CPV y se vuelve a calibrar.

El protocolo de la estación no lleva identificador de secuencia: no es posible distinguir un duplicado tardío de un reintento deliberado. Por eso un ACTIVAR recibido después del pulso cuenta como nuevo intento.

## Recepción prioritaria

- Tarea LoRa/motor de prioridad 3 en núcleo 1. Es la única que utiliza SPI/LoRa y controla las salidas.
- Sensores y calibración en núcleo 0. Las esperas de conversión BMP180 y los errores I²C no bloquean la tarea de radio.
- SX127x en recepción continua; se consulta DIO0/RxDone. Solo se llama a `parsePacket()` al terminar un paquete y se vuelve a `LoRa.receive()` inmediatamente después de leerlo, antes de ejecutar la orden. La biblioteca pasa brevemente por standby al procesar cada paquete; no se afirma una recepción RF ininterrumpida garantizada.
- **No hay transmisiones LoRa:** ni telemetría ni ACK. El mensaje `Enviado` de la estación confirma su transmisión, no que la CPV recibió o abrió. Los eventos recibidos se registran únicamente por USB en la CPV.
- El temporizador de motor usa `millis()` y no necesita nuevos datos de sensores o nuevos paquetes. No hay esperas de 1 o 5 s bloqueando radio.
- Si LoRa no se detecta al iniciar, se intenta de nuevo cada segundo mientras el motor está apagado; el automático puede seguir operando con sensores. No hay un bucle infinito de error que detenga el programa completo.
- El cable DIO0 es necesario para detectar paquetes completos. El software no puede garantizar la recepción si falta alimentación, conexión, cobertura o falla el módulo.

Configuración idéntica a la estación: **433 MHz, SF7, BW125 kHz, CR4/5, sync word 0x12, preámbulo 8, CRC deshabilitado, IQ normal, cabecera explícita**.

## Conexiones

| Elemento | GPIO ESP32 |
|---|---|
| I²C SDA / SCL | 21 / 22 |
| LoRa SCK / MISO / MOSI | 18 / 19 / 23 |
| LoRa NSS / RESET / DIO0 | 5 / 14 / 2 |
| Entrada A / B del controlador de motor | 25 / 26 |

Motor mediante su controlador y alimentación adecuada, con GND común. Los pines GPIO son señales de control. Apertura: **25 HIGH, 26 LOW**; cierre: **25 LOW, 26 HIGH**, según el sentido que se probó en SIM.

## Estimación con sensores reales

- MPU6050 en dirección 0x68. Se configura explícitamente ±16 g y ±2000 °/s, con sensibilidades 2048 cuentas/g y 16.4 cuentas/(°/s). El rango anterior de ±2 g resulta limitado para aceleraciones grandes. Incluso ±16 g puede saturarse: no elimina ese límite físico. Registros y escalas: [documentación TDK MPU6050](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf).
- BMP180 en 0x77, compensación por sus coeficientes de fábrica y comprobación de errores I²C/divisiones. Mismo método por registros que la CPV original; conversión con OSS=0.
- Referencia de presión promedio y promedio del giro a partir de 100 muestras aproximadamente estacionarias a 50 Hz. Los criterios de reposo son magnitud de aceleración cercana a g y giro pequeño; debe mantenerse físicamente inmóvil. La compensación de aceleración es un ajuste escalar en reposo, no una calibración completa de seis posiciones.
- La vertical se inicializa desde la gravedad en reposo y se propaga con los tres ejes del giróscopo. Se proyectan las tres componentes del acelerómetro sobre esa vertical y se resta g. Esto permite una orientación inicial inclinada; no se presupone que el eje Z del sensor siempre sea vertical.
- No se usa el magnetómetro HMC5883L: el rumbo no es necesario para proyectar la vertical. La orientación se integra con el giro y puede derivar. Esta implementación necesita validación de sesgo, vibraciones, retardos y rango con datos reales.
- Se conservan las ecuaciones y parámetros de la simulación: altura logarítmica, velocidad por diferencia barométrica, integración trapezoidal y filtro complementario con tau=0.4 s. Se suaviza la aceleración de predicción con constante 0.1 s; T del modelo=293.15 K y g=9.81 m/s². Referencia teórica: `DocTec/main.pdf`, ecuaciones 2, 6, 7, 25 y 32.
- Solo se decide con lecturas nuevas de presión; no se reutiliza una lectura fallida como nueva. Lecturas inválidas de presión no actualizan la estimación. Después de una interrupción >0.25 s se restablece la base temporal sin inventar velocidad del intervalo perdido.
- Si la aceleración se satura, se usa velocidad barométrica y se exige 0.5 s de aceleración válida antes de volver a predecir. Una pérdida o saturación del giróscopo invalida la orientación hasta una nueva calibración, dejando el respaldo por descenso barométrico y por radio. No se reinicializa orientación desde aceleraciones de vuelo.
- El criterio de respaldo automático sigue siendo Vr < −0.5 m/s y caída ≥0.5 m desde el máximo durante 0.15 s continuos. Si fallan ambos sensores, no puede evaluar ese respaldo; ACTIVAR por LoRa sigue operativo.

Los umbrales y filtros son valores de ensayo del proyecto, no parámetros validados de vuelo. El archivo `.e` no se carga en este firmware: la detección usa mediciones reales. La predicción de 2 s es una extrapolación local, no conocimiento anticipado del apogeo.

## Verificación realizada

- Compilación para ESP32 clásico con core 3.3.7 y LoRa 0.8.0.
- Pruebas C++ ejecutadas en PC: comandos exactos de la estación, rechazos de formato, cierre de 1 s, apertura de 5 s, inversión prioritaria, comandos durante apertura, reintento posterior, bloqueo de ARMAR en vuelo y desbordamiento del reloj.
- Proyección vertical para giro de 90°, pérdida de orientación por salto temporal, compensación BMP180 con valores de referencia (69964 Pa), reposo sin activación, recuperación tras interrupción del barómetro y reproducción de las 2229 efemérides del archivo de prueba.
- Con la trayectoria sintética: orden anticipada a 12.78 s; con predicción inercial deshabilitada, respaldo por descenso a 15.24 s. Son pruebas de la lógica, no del circuito físico ni de la radio.
- Se verificó que el SHA256 de la estación terrena coincide antes y después: `C8561C68F30FC02D8FB948C6246805F2E66EA3DAE0887B6BC2CA127CA78A02E8`.

Para repetir las pruebas desde `Codes` con Visual Studio Community 2022 instalado:

```powershell
& Proyecto_HC2026_RW/CPV_Paracaidas_LoRa_RX/tests/run.cmd
& tools/arduino-local/arduino-local.exe compile --fqbn esp32:esp32:esp32 --build-path build/cpv-lora-rx Proyecto_HC2026_RW/CPV_Paracaidas_LoRa_RX
```

Los sustitutos I²C dentro de `tests/stubs` solo permiten compilar las pruebas numéricas en PC; no simulan ni prueban el bus o el hardware.
