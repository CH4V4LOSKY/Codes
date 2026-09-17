# Verificar y cargar el archivo abierto

Abre Codes como carpeta de trabajo y enfoca el editor de un archivo `.ino`.

- **Ctrl+Alt+R**: verifica el sketch del archivo abierto.
- **Ctrl+Alt+U**: compila y, solo si no hay errores, carga ese sketch.
- **Ctrl+Shift+B**: ejecuta tambien la verificacion del archivo abierto.

Estas tareas guardan los archivos antes de ejecutarse. Usan la placa, opciones
y puerto de `.vscode/arduino.json`, pero ignoran su campo `sketch`.
Arduino compila todos los archivos que forman el sketch de esa carpeta.
La seleccion automatica del archivo no cambia automaticamente la placa.

Los botones originales Arduino: Verify y Arduino: Upload de la extension
siguen usando su propio sketch seleccionado. Para este flujo usa los atajos o
**Tasks: Run Task > Arduino: Verificar archivo abierto / Cargar archivo abierto**.

Los atajos Ctrl+Alt+R y Ctrl+Alt+U estan configurados en las preferencias del
usuario de VS Code, limitados a archivos `.ino` de Rckect_2026/Codes. Al clonar
en otro equipo, las tareas y Ctrl+Shift+B siguen disponibles; los otros dos
atajos se pueden agregar con `workbench.action.tasks.runTask` y el nombre de
la tarea como argumento.

Las tareas usan el lanzador local existente para resolver `Codes/libraries`.
Si falta una dependencia, agregala a esa carpeta. Si falta el lanzador en una
copia nueva del repositorio, ejecuta `setup.ps1` segun libraries/README.md.
