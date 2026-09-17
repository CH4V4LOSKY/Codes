# UI de comandos de la estación terrena

La UI se conecta por USB al ESP32 con `Proyecto_HC2026_RW/EstacionTerrena_LoRa_Lib/EstacionTerrena_LoRa_Lib.ino`. La estación actual **solo transmite comandos por LoRa**: no recibe telemetría ni confirmaciones. Las gráficas vacías y los indicadores `--` son normales. Los botones no esperan muestras ni ACK.

## Uso

Desde la carpeta `Codes`:

```powershell
node ui/server.mjs
```

Abrir **http://127.0.0.1:4173** en Chrome o Edge. Mantener el servidor abierto. Pulsar Conectar y elegir el puerto USB de la estación, a 115200 baudios. Cerrar otros monitores seriales que utilicen ese puerto. Este panel es distinto de `SIM` en el puerto 4174, que se conecta directamente al firmware de simulación de la CPV.

| Botón o entrada | Bytes USB a la estación | Paquete LoRa | Acción en Freddy |
|---|---|---|---|
| ARMAR | `ARMAR` + nueva línea | `ARMAR` | Servo a 0° |
| ACTIVAR | `ACTIVAR` + nueva línea | `ACTIVAR` | Servo a 90° |

El campo de texto elimina espacios exteriores y convierte a mayúsculas. Solo admite ARMAR y ACTIVAR; no envía órdenes desconocidas ni varias órdenes dentro de una misma entrada. No hay reenvío automático. Se puede enviar de nuevo manualmente si es necesario.

## Qué confirma la pantalla

- **Enviado por USB:** terminó la escritura al puerto de la estación; aún no confirma transmisión de radio ni actuación.
- **La estación informó transmisión LoRa:** se recibió su mensaje `Enviado: ARMAR` o `Enviado: ACTIVAR`. No confirma recepción del receptor ni posición del mecanismo.
- Sin puerto conectado o durante Demo, la UI indica que el comando no fue enviado.
- Los valores de sensores empiezan en `--`. Demo muestra datos sintéticos explícitamente marcados; al conectar una estación se borran los valores anteriores de Demo.

## Compatibilidad con Freddy

Archivo verificado sin modificar: `Proyecto_HC2026_RS/code_Fredy/code_Fredy.ino`.

- La placa del código actual de Freddy es **Arduino Nano clásico ATmega328P**, no ESP32.
- Bibliotecas: **Servo** y **LoRa** (Sandeep Mistry).
- Servo en **D5**, orden inicial de 0° al encender.
- LoRa Nano: SCK D13, MISO D12, MOSI D11, NSS D10, RESET D9, DIO0 D2.
- LoRa estación ESP32: SCK 18, MISO 19, MOSI 23, NSS 5, RESET 14, DIO0 2.
- Ambos coinciden: 433 MHz, SF7, ancho de banda 125 kHz, CR4/5, sync word 0x12, preámbulo 8, CRC deshabilitado e IQ normal. La biblioteca usa cabecera explícita por defecto.
- No se utiliza `CMD:<seq>:<comando>` ni `ACK:<seq>:<comando>`. Freddy acepta únicamente los paquetes directos ARMAR y ACTIVAR.

La CPV nueva `Proyecto_HC2026_RW/CPV_Paracaidas_LoRa_RX` también acepta esas órdenes. Sus salidas controlan el motor, mientras Freddy controla un servo. No hay dirección de destinatario: si ambos receptores están encendidos y dentro de alcance con esta misma configuración, ambos pueden actuar ante la misma orden.

## Verificación realizada

- La estación compiló para ESP32 (core 3.3.7, LoRa 0.8.0).
- Freddy compiló para Nano ATmega328P (core AVR 1.8.7, Servo 1.3.0, LoRa 0.8.0), usando la biblioteca Servo ya instalada.
- Prueba de la UI con puerto simulado: bytes exactos ARMAR + LF / ACTIVAR + LF, envío sin telemetría/ACK, normalización, entradas inválidas, error USB y mensajes de transmisión.
- Prueba C++ que incluye ambos sketches originales sin cambios, sustituye solo Serial/LoRa/Servo y ejecuta sus funciones: bytes generados por la UI → estación → paquetes ARMAR/ACTIVAR → órdenes de servo 0°/90°. También verifica parámetros coincidentes y fin de línea CRLF sin envío duplicado.
- Revisión visual de la UI: botones presentes, sensores sin datos y aviso correcto al intentar enviar sin conexión.
- **No se realizó una prueba física de radio ni movimiento de servo/motor.** Los sustitutos de prueba verifican el protocolo y las llamadas del código, no la propagación RF ni el hardware.

Para repetir las pruebas en PC con Node.js y Visual Studio Community 2022:

```powershell
ui/tests/run.cmd
```

Hashes SHA256 antes/después, iguales:

- Estación: `C8561C68F30FC02D8FB948C6246805F2E66EA3DAE0887B6BC2CA127CA78A02E8`
- Freddy: `106D52E94C4AA6A5E2E62C0BB9EE1B861F405FBC7CD9358B1898AAD98BFB696A`

Los parsers antiguos de telemetría se conservan para Demo o un emisor compatible; no se usan como requisito para enviar órdenes con la estación actual.
