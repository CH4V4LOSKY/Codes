@echo off
setlocal
"%~dp0..\.tools\tectonic\tectonic.exe" "%~dp0main.tex"
if errorlevel 1 (
    echo Error al compilar el documento.
    pause
    exit /b 1
)
start "" "%~dp0main.pdf"
