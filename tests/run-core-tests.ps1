$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compiler = 'C:\devkitPro\msys2\usr\bin\gcc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'No se encuentra el compilador de pruebas de devkitPro.' }
$env:PATH = "C:\devkitPro\msys2\usr\bin;$env:PATH"
Push-Location -LiteralPath $projectRoot
try {
    & $compiler -std=c99 -Wall -Wextra -Werror -O2 -Iinclude tests/core_test.c source/guardian_core.c source/guardian_sha256.c source/guardian_addons.c -o tests/core_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'No se pudieron compilar las pruebas del motor.' }
    & ./tests/core_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Las pruebas del motor han fallado.' }
    & $compiler -std=c99 -Wall -Wextra -Werror -O2 -Iinclude tests/recovery_test.c source/guardian_core.c source/guardian_sha256.c source/guardian_addons.c source/guardian_recovery.c -o tests/recovery_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'No se pudieron compilar las pruebas de recuperacion integrada.' }
    & ./tests/recovery_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Las pruebas de recuperacion integrada han fallado.' }
    & $compiler -std=c99 -Wall -Wextra -Werror -O2 -Iinclude tests/discard_test.c source/guardian_core.c source/guardian_sha256.c source/guardian_addons.c source/guardian_recovery.c -o tests/discard_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'No se pudieron compilar las pruebas de eliminacion confirmada.' }
    & ./tests/discard_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Las pruebas de eliminacion confirmada han fallado.' }
} finally { Pop-Location }
