# Librerias de este proyecto

Abre la carpeta **Codes** completa en VS Code. Arduino Community utiliza el
lanzador de `tools/arduino-local` para establecer esta carpeta como sketchbook
solo durante sus procesos. Las librerias externas se buscan e instalan en
`Codes/libraries`. No se modifica el sketchbook global de Documentos.

`Wire`, `SPI` y otras librerias incluidas con las placas siguen viniendo del
paquete de la placa instalado en Arduino15. Selecciona la placa adecuada para
cada sketch: la configuracion inicial de este repositorio selecciona Arduino Nano.

## Agregar una libreria

Desde una terminal PowerShell abierta en Codes:

```powershell
.\tools\arduino-local\arduino-local.exe lib install "Nombre de la libreria"
```

Tambien puedes descomprimir una libreria en `libraries/Nombre/`, conservando
`library.properties`, `src`, ejemplos y licencia. Solo se compilan las librerias
que necesita el sketch a traves de sus `#include` y dependencias.

## Repositorio y otro equipo

Incluye `libraries`, `tools/arduino-local`, `.vscode` y `.gitignore` en tu commit.
Las fuentes completas de las librerias quedan versionadas; conserva sus licencias.
El ejecutable auxiliar y `build` se excluyen de Git.

Al clonar en otro equipo Windows, o mover Codes a otra ruta, instala Arduino
Community y los paquetes de tus placas y ejecuta desde Codes:

```powershell
.\tools\arduino-local\setup.ps1
```

Esto recompila el auxiliar con .NET Framework de Windows y actualiza la ruta de
Arduino en la configuracion de este espacio de trabajo. Luego ejecuta
**Developer: Reload Window** en la paleta de comandos de VS Code.
Los comandos normales **Arduino: Verify** y **Arduino: Upload** usaran el auxiliar.
Para listar las librerias locales:

```powershell
.\tools\arduino-local\arduino-local.exe lib list
```

Referencia: https://arduino.github.io/arduino-cli/0.35/configuration/

## Verificacion realizada

LoRaReceiver compilo correctamente para `arduino:avr:nano:cpu=atmega328`
usando `Codes/libraries/LoRa` 0.8.0 (3816 bytes de programa, 312 bytes de RAM).
No se cargo firmware a la placa.

El sketch CPV_LoRa_Telemetria_Lib selecciona correctamente LoRa local, pero
presenta errores de sintaxis y utiliza llamadas Wire/SPI de ESP32 mientras la
placa configurada es Nano. Estos problemas del sketch requieren revision aparte.
