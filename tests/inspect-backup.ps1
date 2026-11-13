param(
    [Parameter(Mandatory=$true)][string]$SdRoot,
    [switch]$RescueFormatter
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compiler = 'C:\devkitPro\msys2\usr\bin\gcc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'No se encuentra el compilador de pruebas.' }
$resolvedRoot = (Resolve-Path -LiteralPath $SdRoot).Path.Replace('\','/')
$env:PATH = "C:\devkitPro\msys2\usr\bin;$env:PATH"
Push-Location -LiteralPath $projectRoot
try {
    $arguments = @('-std=c99','-Wall','-Wextra','-Werror','-O2','-Iinclude',
        'tests/core_test.c','source/guardian_core.c','source/guardian_sha256.c',
        'source/guardian_addons.c')
    $program = 'tests/core-inspect.exe'
    if ($RescueFormatter) {
        $arguments += @('-Dsnprintf=guardian_snprintf','rescue/guardian_format.c')
        $program = 'tests/core-inspect-rescue.exe'
    }
    $arguments += @('-o',$program)
    & $compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo compilar el diagnóstico de solo lectura.' }
    & "./$program" --inspect-root $resolvedRoot
    $diagnosticExit = $LASTEXITCODE
    if ($diagnosticExit -ne 0) { throw "El diagnóstico detectó un problema (código $diagnosticExit). No se modificó la copia." }
} finally { Pop-Location }
