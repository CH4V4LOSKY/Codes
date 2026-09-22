# UI de telemetría y comandos CPV

Usar la nueva estación `Proyecto_HC2026_RW/CPV_Paracaidas_LoRa_RX/EstacionTerrena/EstacionTerrena.ino`, conectada por USB a 115200 baudios. Cargar el sketch principal de esa carpeta en la CPV.

Desde `Codes`:

```powershell
node ui/server.mjs
```

Abrir http://127.0.0.1:4173 en Chrome o Edge, pulsar **Conectar** y seleccionar el USB de la estación. Cerrar otros monitores seriales de ese puerto.

## Pantalla

- Secuencia **ENCENDER → ARMAR → CALIBRAR**. ARMAR solo cierra durante 1 s; esperar **ARMADO** y pulsar **CALIBRAR** con la CPV inmóvil durante ~2 s (100 muestras filtradas y estables). Se eliminan picos aislados y se evalúa la dispersión para tolerar el ruido y el sesgo que se está calibrando. La calibración solo empieza por esta orden, no por encender ni armar.
- **LISTO PARA VUELO** requiere armado terminado, calibración manual completa, IMU/barómetro/orientación válidos y telemetría reciente. Un ACK confirma la solicitud; no confirma el fin del cierre ni de la calibración.
- La detección automática permanece activa con la primera referencia de presión aunque falten pasos de preparación. Sin calibración manual funciona el respaldo barométrico; la calibración añade la referencia promediada y la orientación para la predicción inercial.
- Si comienza el vuelo durante la calibración, esta se cancela sin reiniciar el vuelo. Si en 15 s no consigue reposo y lecturas válidas, muestra **CALIBRACIÓN NO COMPLETADA** y requiere volver a pulsar CALIBRAR. Nunca reintenta la medición por sí sola.

- **Paracaídas rojo:** aún no hay confirmación de activación. **Verde:** la CPV reportó su orden de apertura, automática o manual. El clic de ACTIVAR y la transmisión USB no cambian por sí solos el color. No hay sensor de posición física.
- Recuadros de **velocidad máxima |Vz|**, **aceleración máxima |az|** y **altura máxima**. Llegan acumulados desde la CPV, incluso si se perdieron paquetes o se reconecta la UI.
- Velocidad vertical y aceleración neta filtrada corresponden a la estimación de la CPV. La aceleración máxima excluye datos inválidos/saturados; las unidades están en pantalla.
- Sensores sin datos se muestran como `--`. La versión actual no transmite temperatura ni rumbo; esos campos permanecen vacíos.
- Después de 2 s sin telemetría, se indica pérdida de actualización y se conservan los últimos máximos y el estado del paracaídas.
- **Limpiar** vacía gráficas/registro y contadores de recepción, pero conserva máximos y activación. Los máximos se reinician al reiniciar la CPV o al **completar** una calibración manual antes del vuelo. ARMAR o una calibración fallida no los borran. No son persistentes al apagarla.
- **CSV** exporta las últimas 180 muestras de las gráficas, incluidos máximos, activación y origen. La memoria de máximos no depende de ese límite.
- **Demo** muestra datos simulados, incluyendo cambio a verde por activación automática a los ~34 s. En Demo no se transmiten órdenes.

## Reiniciar y sonidos

- **Reiniciar CPV** reinicia el ESP32 de la CPV por LoRa. Borra su calibración, estado de vuelo y máximos; la UI vuelve al estado inicial al recibir el nuevo arranque. No cierra físicamente el paracaídas: después se vuelve a ARMAR y CALIBRAR.
- La estación debe haber recibido telemetría para dirigir el reinicio al arranque actual. Los reintentos viejos no provocan otro reset después de arrancar. El reinicio espera que termine cualquier pulso del motor; una apertura manual o automática nueva cancela un reinicio pendiente.
- La UI distingue **reinicio aceptado**, **nuevo arranque confirmado** y **sin confirmación de nuevo arranque en 15 s**. Pulsar el botón o recibir el ACK no borra la pantalla por anticipado.
- **Sonido: activado/silenciado**, control de **Volumen** y **Probar sonido** funcionan de manera independiente de la conexión. Sonidos por los altavoces de la computadora; no se añadió un buzzer físico.
- **Probar sonido** reproduce la misma alarma que la confirmación del paracaídas: tres ráfagas de zumbido grave pulsante y tonos agudos alternados, de unos cuatro segundos. Es una síntesis original inspirada en alarmas de cabina. La prueba no envía comandos ni cambia el estado mostrado.
- El aviso automático de **paracaídas activado** suena únicamente al recibir esa confirmación por telemetría, automática o manual. Se emite una vez por arranque, incluso si los paquetes repiten el estado, se limpia la gráfica o se reconecta el USB en esta misma página. No suena por pulsar ACTIVAR ni por recibir solamente el ACK del comando.
- También hay avisos de armado terminado, calibración/preparación completa, nuevo arranque y pérdida de telemetría (una vez por interrupción). La alerta de paracaídas tiene prioridad sobre otros tonos. Demo usa sus propios eventos simulados.
- El navegador necesita una interacción para habilitar audio: Conectar, Demo, un botón de comando o Probar sonido. Silenciar/volumen cero no afecta las órdenes. Si el navegador no permite audio, telemetría y comandos siguen funcionando.
- `sounds.js` contiene el audio separado de `app.js`; genera tonos locales, sin descargar archivos ni depender de Internet.

## Protocolo de comandos

UI → USB → estación → LoRa → CPV → telemetría de confirmación → estación → UI.

| Comando | Acción |
|---|---|
| ARMAR | Solo cierre de 1 s; no reinicia referencia ni calibra. |
| CALIBRAR | Medición manual en reposo; no acciona el motor ni detiene la detección de vuelo. |
| ACTIVAR | Apertura prioritaria de 5 s; funciona sin calibración o telemetría recibida. |
| REINICIAR | Reinicio del ESP32 de la CPV y regreso a su estado lógico inicial; exige un arranque destinatario conocido. |

La UI envía `ARMAR\n`, `CALIBRAR\n`, `ACTIVAR\n` o `REINICIAR\n`. La estación agrega sesión, secuencia y arranque destinatario para reinicio, repite hasta 8 intentos y espera un ACK dentro de la telemetría. Las retransmisiones de la misma intención no repiten el motor ni reinician la calibración. Un nuevo ACTIVAR después de finalizar la orden anterior es un reintento manual deliberado. Esperar que termine ARMAR antes de solicitar CALIBRAR. ACTIVAR tiene prioridad sobre preparación y reinicio; la preparación se rechaza durante el vuelo o después de abrir.

No cambiar de verde a rojo por desconectar el USB o por perder un paquete. Un reinicio de la CPV o una nueva referencia de calibración inician una nueva memoria.

## Compatibilidad y pruebas

Para CALIBRAR hay que actualizar **el firmware de la CPV, el de la estación y recargar esta UI**. La primera referencia barométrica se muestra aun sin calibración manual. Al desconectar o perder telemetría, se retira LISTO PARA VUELO; se conservan los máximos y el estado de apertura.

Se conserva la lectura de las tramas antiguas `UI_TLM,TLM,...` y de los mensajes `Enviado: ...`; esos formatos no confirman el estado del paracaídas. La estación vieja de `EstacionTerrena_LoRa_Lib` solo transmite comandos y no ofrece estas funciones nuevas. La estación nueva usa CPV2/CMD2 y está destinada a la CPV de esta carpeta.

```powershell
Proyecto_HC2026_RW/CPV_Paracaidas_LoRa_RX/tests/run.cmd
ui/tests/run.cmd
```

Pruebas: bytes exactos, comandos sin telemetría, errores USB, Demo, identificación de ACK, rojo/verde automático/manual, máximos que sobreviven a la ventana de gráficas, sensores ausentes, reinicio y duplicados. Se conserva la prueba de compatibilidad de la UI con los sketches antiguos.

No se realizó una prueba física de enlace ni de apertura. Detalles de pines, radio y protocolo: `Proyecto_HC2026_RW/CPV_Paracaidas_LoRa_RX/README.md`.
