param(
    [Parameter(Mandatory = $true)][string]$SketchFile,
    [ValidateSet('verify', 'upload')][string]$Action = 'verify',
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
try {
    $projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
    $activeFile = (Resolve-Path -LiteralPath $SketchFile).Path
    if ([IO.Path]::GetExtension($activeFile) -ne '.ino') { throw 'Abre un archivo .ino antes de ejecutar esta tarea.' }
    if (-not $activeFile.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'El archivo abierto debe pertenecer a este proyecto.'
    }
    $sketchDirectory = Split-Path $activeFile -Parent
    $sketchName = Split-Path $sketchDirectory -Leaf
    if (-not (Test-Path -LiteralPath (Join-Path $sketchDirectory ($sketchName + '.ino')))) {
        throw 'Arduino requiere un archivo .ino con el mismo nombre que su carpeta.'
    }
    $config = Get-Content (Join-Path $projectRoot '.vscode\arduino.json') -Raw | ConvertFrom-Json
    if (-not $config.board) { throw 'Selecciona primero una placa en Arduino: Board Config.' }
    $fqbn = $config.board
    if ($config.configuration) { $fqbn += ':' + $config.configuration }
    if ($Action -eq 'upload' -and -not $config.port) { throw 'Selecciona primero el puerto de la placa.' }
    $relativeSketch = $sketchDirectory.Substring($projectRoot.Length + 1)
    $buildPath = Join-Path $projectRoot ('build\active\' + $relativeSketch)
    Write-Host "Sketch activo: $activeFile"
    Write-Host "Placa: $fqbn"
    if ($Action -eq 'upload') { Write-Host "Puerto: $($config.port)" }
    if ($CheckOnly) { exit 0 }
    $cli = Join-Path $PSScriptRoot 'arduino-local.exe'
    if (-not (Test-Path $cli)) { throw 'Ejecuta tools\arduino-local\setup.ps1 para preparar Arduino local.' }
    & $cli compile --fqbn $fqbn --build-path $buildPath $sketchDirectory
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Action -eq 'upload') {
        & $cli upload --fqbn $fqbn --port $config.port --input-dir $buildPath $sketchDirectory
        exit $LASTEXITCODE
    }
    exit 0
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
