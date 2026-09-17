# CPV: simulación del paracaídas

Banco de pruebas basado en `Proyecto_HC2026_RW/CPV_LoRa_Telemetria_Lib/CPV_LoRa_Telemetria_Lib.ino` y `DocTec/main.pdf`. El programa original se conserva. Esta variante utiliza la misma plataforma ESP32, comunicación Serial a 115200 y pines de motor 25/26; sustituye los sensores GY-87 y el transporte LoRa por inyección de muestras por USB. No necesita LoRa ni sensores conectados para esta prueba.

## Abrir la interfaz

Desde la carpeta del proyecto, ejecutar en PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File SIM/iniciar.ps1
```

Abrir **http://127.0.0.1:4174** en Chrome o Edge. Mantener la terminal abierta. También se puede ejecutar `node SIM/server.mjs`. No abrir `index.html` directamente como archivo.

La interfaz carga `350deg_AR_4_57ms_Aire.e` por defecto; también permite seleccionar otro `.e` con formato STK `EphemerisLLATimePos`, metros y tiempos crecientes desde cero.

1. Seleccionar **Simulación local** y pulsar **Iniciar prueba** para probar sin placa.
2. Ajustar la reproducción a 1× para comparar segundos simulados y reales. Otros ritmos no cambian los intervalos usados por el filtro.
3. Pausar, continuar o reiniciar. El evento de activación queda marcado en naranja.
4. Exportar las muestras procesadas y resultados a CSV.

## Probar en la CPV ESP32

1. Abrir `CPV_Paracaidas_SIM/CPV_Paracaidas_SIM.ino` en Arduino IDE y seleccionar **ESP32 Dev Module** (`esp32:esp32:esp32`). Esta variante no está escrita para Arduino Nano AVR.
2. Cargar el sketch por USB. Cerrar el monitor serial para liberar el puerto.
3. En la interfaz pulsar **Conectar ESP32**, elegir el puerto USB y luego **Iniciar prueba**.
4. El navegador envía presión y fuerza específica proyectada. **El ESP32 calcula Vb, Va, Vr, la predicción y la decisión**. Las gráficas usan su respuesta, no el cálculo local.
5. La orden activa el motor en el mismo sentido que `DESARMAR`: GPIO25 HIGH y GPIO26 LOW durante 5000 ms reales, luego ambos LOW. Conectar estos pines a las entradas del controlador de motor, no directamente al motor. Hay un solo pulso por prueba. RESET y STOP apagan la salida inmediatamente. El temporizador no bloquea la recepción USB y apaga el motor aunque dejen de llegar muestras. GPIO2 ya no se utiliza.

No se ha cargado ni probado este sketch en una placa física desde esta tarea. La compilación y el modo local se verifican por separado. No usar este firmware de inyección como firmware autónomo de vuelo: espera muestras USB y no estima orientación real.

## Relación con el PDF

- Altura relativa: `hb = (287 T/g) ln(p0/p)` (ecuación 2).
- Aceleración: `az = fz,N - g` (ecuaciones 19/32). La entrada ya está proyectada a la vertical local; no es el eje Z crudo del MPU6050.
- Predicción inercial por trapecios: `Va = Vr_anterior + (az_anterior + az)/2 * dt` (ecuación 6).
- Filtro complementario: `Vr = alpha Va + (1-alpha) Vb`, `alpha = tau/(tau+dt)` (ecuación 7).
- Predicción local al apogeo: `eta = -Vr/az_filtrada`, solo si `Vr > 0` y `az_filtrada < -0.1 m/s²` (ecuación 25). En otro caso se devuelve -1 y se muestra una raya.
- La aceleración se suaviza con un filtro de primer orden de constante 0.1 s para la predicción. La integración usa aceleración sin ese suavizado adicional.

Se usa T=293.15 K, g=9.81 m/s² y tau=0.4 s inicialmente. El PDF presenta tau=0.4 como ejemplo didáctico, no como ajuste validado. La interfaz permite cambiar tau antes de iniciar.

El PDF no define una regla completa de despliegue. Por petición del usuario, después de reconocer ascenso (altura ≥3 m y Vr ≥3 m/s), se emite una única orden en la primera muestra con predicción válida `0 < eta <= 2 s`. El actuador tarda 1 s: la interfaz muestra la liberación estimada un segundo después de la orden, usando tiempo simulado. No es confirmación física de apertura. Si la predicción no activa, se conserva el respaldo: Vr < -0.5 m/s y caída ≥0.5 m desde el máximo durante 0.15 s continuos. El despliegue queda enclavado hasta RESET. La anticipación es una decisión de este ensayo basada en la predicción local; no equivale a una confirmación medida del apogeo. Los 2 s son respecto al apogeo estimado, no al máximo conocido del archivo.

## Qué representa el archivo

El archivo tiene 2229 puntos de tiempo, latitud, longitud y altura, desde 0 hasta 31.818 s. No contiene presión, acelerómetro, orientación, apertura de paracaídas ni datos medidos de sensores.

- Se resta la altura inicial de 2347 m para construir altura relativa. No se interpreta la referencia geodésica absoluta como presión medida.
- Se interpola linealmente a 50 Hz hasta 31.80 s (1591 muestras). Los últimos 0.018 s quedan fuera de esta rejilla.
- Se estima velocidad por diferencias sobre una ventana de hasta 0.20 s y aceleración repitiendo la operación. El generador offline puede usar puntos futuros; el filtro de la CPV solo recibe la muestra actual.
- Se genera `p=101325 exp(-h/K)` con `K=287 T/g` y `fz,N=az+g`. Los 101325 Pa son una referencia sintética, no la presión real del sitio a 2347 m.
- Se asume una entrada vertical ya proyectada y sin sesgo. No se inventa actitud del cohete a partir de su dirección de movimiento, ni se simula el rango/saturación del MPU6050.
- Ambas entradas proceden de la misma altura: no son fuentes independientes. El resultado verifica ejecución y temporización de la lógica, no precisión experimental frente a ruido, orientación o retardos de sensores reales.
- La trayectoria sigue igual tras la activación. Esta prueba no modela la apertura física ni la nueva dinámica de descenso con paracaídas.

## Protocolo USB

Una línea por mensaje, terminada en LF. Solo una muestra pendiente a la vez; no se descartan muestras para alcanzar el reloj. Si USB va lento, se muestra el retraso y la reproducción tarda más. Una respuesta ausente durante 2 s detiene la prueba. Pausar detiene las nuevas muestras; la muestra en curso puede terminar. El estado de despliegue queda retenido hasta reinicio.

```text
HELLO -> READY,CPV_SIM,4,MOTOR
RESET -> RESET_OK
CFG,0.4 -> CONFIG_OK
ARM -> ARMED
S,secuencia,tiempo_s,presion_Pa,fuerza_vertical_m_s2
R,secuencia,tiempo_s,altura_m,Vr,Vb,Va,az_filtrada,eta_s,estado,activado
STOP -> STOPPED
```

Estados: 0 espera de ascenso, 1 ascenso reconocido, 2 orden de activación enclavada. `activado` vale 0 o 1. Muestras inválidas, repetidas, fuera de orden o con intervalo >0.25 s se rechazan sin actualizar la estimación. Después de STOP se requiere RESET para una nueva sesión que ya tenía muestras.

## Verificación

```powershell
node SIM/test.mjs
& tools/arduino-local/arduino-local.exe compile --fqbn esp32:esp32:esp32 --build-path build/sim-cpv SIM/CPV_Paracaidas_SIM
```

Con los valores iniciales: máximo interpolado a 14.74 s y 1177.335 m relativos; orden a 12.78 s, con eta=1.975 s, altura 1157.556 m y Vr=20.166 m/s. Liberación estimada a 13.78 s: 0.96 s antes del máximo del archivo. La orden se anticipa 1.96 s al máximo. Es una referencia de reproducción, no una garantía de desempeño en vuelo.

Las pruebas verifican el ejemplo numérico del PDF (~8.328 m/s), reposo sin activación, descenso sin lanzamiento, tiempos inválidos y un único evento de despliegue en la trayectoria completa.

La interfaz exige firmware CPV_SIM v4 MOTOR para evitar usar accidentalmente versiones con salida LED. Volver a cargar el sketch actualizado en el ESP32.

Al iniciar en modo USB se selecciona automáticamente 1× para mantener la reproducción en tiempo real. Pausar la reproducción no pausa un pulso de motor ya iniciado: termina al cumplirse 5000 ms reales. La liberación en pantalla sigue siendo una estimación en tiempo de trayectoria; una pausa o retraso USB puede desalinearla del mecanismo. Reiniciar apaga el motor.

El pulso del motor dura 5 s para completar el desenroscado. Es independiente del retardo de liberación estimado de 1 s, que se conserva en la interfaz. La orden sigue anticipándose 2 s al apogeo estimado.

## Pruebas manuales del mecanismo

Conectar el ESP32 con firmware v4. Fuera de una reproducción activa o pausada, los botones **Armar mecanismo · 1 s** y **Desarmar mecanismo · 1 s** permiten probar el paracaídas:

- `ARMAR`: GPIO25 LOW, GPIO26 HIGH, durante 1000 ms reales.
- `DESARMAR`: GPIO25 HIGH, GPIO26 LOW, durante 1000 ms reales.
- Al terminar, ambos pines quedan LOW. Respuestas: `OK,ARMAR` y luego `DONE,ARMAR`, o sus equivalentes para DESARMAR. DONE confirma el fin del pulso, no la posición mecánica.
- La interfaz envía STOP antes del comando manual para deshabilitar la lógica automática. ARMAR no habilita el vuelo: `ARM` sigue siendo el comando independiente para habilitar la recepción de muestras.
- Desde monitor serial: 115200 baudios y terminación nueva línea. Enviar STOP, luego ARMAR o DESARMAR. Esperar DONE antes de otro movimiento. STOP/RESET interrumpen el pulso inmediatamente.
- No se aceptan movimientos manuales si el motor ya está encendido o la simulación está armada (`ERR,BUSY`). La activación automática de vuelo mantiene su pulso de 5 s.
