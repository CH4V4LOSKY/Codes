$ErrorActionPreference = 'Stop'
$simNode = Get-Command node -ErrorAction SilentlyContinue
if ($simNode) { $simExecutable = $simNode.Source }
else { $simExecutable = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe' }
if (-not (Test-Path -LiteralPath $simExecutable)) { throw 'Instala Node.js para iniciar la interfaz.' }
Write-Host 'Abre http://127.0.0.1:4174 en Chrome o Edge. Ctrl+C detiene el servidor.'
& $simExecutable (Join-Path $PSScriptRoot 'server.mjs')
