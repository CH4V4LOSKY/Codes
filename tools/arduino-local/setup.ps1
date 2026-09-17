$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:exe "/out:$PSScriptRoot\arduino-local.exe" "$PSScriptRoot\ArduinoLocal.cs"
if ($LASTEXITCODE -ne 0) { throw 'No se pudo crear el lanzador de Arduino.' }
$settingsPath = Join-Path $projectRoot '.vscode\settings.json'
$settings = @{}
if (Test-Path $settingsPath) {
    $existing = Get-Content $settingsPath -Raw | ConvertFrom-Json
    foreach ($property in $existing.PSObject.Properties) { $settings[$property.Name] = $property.Value }
}
$settings['arduino.useArduinoCli'] = $true
$settings['arduino.path'] = $PSScriptRoot
$settings['arduino.commandPath'] = 'arduino-local.exe'
$settings | ConvertTo-Json -Depth 20 | Set-Content $settingsPath -Encoding UTF8
Write-Host 'Configuracion local lista. Recarga la ventana de VS Code.'
