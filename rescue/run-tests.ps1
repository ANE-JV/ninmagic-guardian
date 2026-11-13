$ErrorActionPreference = 'Stop'
$guardianRoot = Split-Path -Parent $PSScriptRoot
$guardianCompiler = 'C:/devkitPro/msys2/usr/bin/gcc.exe'
$guardianTemp = Join-Path $env:USERPROFILE '.codex/tmp'
$guardianPreviousPath = $env:PATH
$env:PATH = "C:/devkitPro/msys2/usr/bin;$env:PATH"
Push-Location -LiteralPath $guardianRoot
try {
    & python rescue/check_entry.py
    if ($LASTEXITCODE -ne 0) { throw 'La entrada de rescate contiene un cambio de pila inseguro.' }
    & $guardianCompiler -std=c99 -Wall -Wextra -Werror -O2 rescue/guardian_cid.c rescue/test_cid.c -o (Join-Path $guardianTemp 'guardian-cid-test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo compilar la prueba de identidad SD.' }
    & (Join-Path $guardianTemp 'guardian-cid-test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Ha fallado la comparación exacta de la identidad SD.' }
    & $guardianCompiler -std=c99 -Wall -Wextra -Werror -O2 rescue/guardian_format.c rescue/test_format.c -o (Join-Path $guardianTemp 'guardian-format-test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo compilar la prueba del formato de rescate.' }
    & (Join-Path $guardianTemp 'guardian-format-test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'No coincide el formato de los archivos del rescate.' }
    # Exercise the shared core's complete host suite with the EXACT formatter
    # linked into the preboot payload, instead of the PC's libc snprintf.
    & $guardianCompiler -std=c99 -Wall -Wextra -Werror -O2 -Dsnprintf=guardian_snprintf -Iinclude tests/core_test.c source/guardian_core.c source/guardian_sha256.c source/guardian_addons.c rescue/guardian_format.c -o (Join-Path $guardianTemp 'guardian-rescue-core-test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo compilar el motor con el formato del rescate.' }
    & (Join-Path $guardianTemp 'guardian-rescue-core-test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Las pruebas del motor de rescate han fallado.' }
} finally {
    Pop-Location
    $env:PATH = $guardianPreviousPath
}
