param([int]$Jobs = 4, [string]$DevkitPro = 'C:/devkitPro')
$ErrorActionPreference = 'Stop'
$guardianRoot = Split-Path -Parent $PSScriptRoot
& python (Join-Path $guardianRoot 'rescue/build_rescue.py') --jobs $Jobs --devkitpro $DevkitPro
if ($LASTEXITCODE -ne 0) { throw 'No se pudo compilar el rescatador de Guardian.' }
