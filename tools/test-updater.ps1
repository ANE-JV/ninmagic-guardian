[CmdletBinding()]
param([string]$DevkitPro='C:\devkitPro',[switch]$RequireNro)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testRoot=[IO.Path]::GetFullPath((Join-Path $env:USERPROFILE '.codex/tmp/ninmagic-guardian-updater-tests'))
$null=New-Item -ItemType Directory -Path $testRoot -Force
$cxx=Join-Path $DevkitPro 'msys2/usr/bin/g++.exe'
$nro=Join-Path $projectRoot 'dist/NinMagic-Guardian.nro'
if (-not (Test-Path -LiteralPath $cxx -PathType Leaf)) { throw "Missing: $cxx." }
if ($RequireNro -and -not (Test-Path -LiteralPath $nro -PathType Leaf)) { throw 'Build the NRO first with tools/build.ps1.' }
function Invoke-Checked([string]$ToolPath,[string[]]$ToolArguments) {
    $normalizedArguments=@($ToolArguments | ForEach-Object { $_.Replace('\','/') })
    & $ToolPath @normalizedArguments
    if ($LASTEXITCODE -ne 0) { throw "Verification failed: $ToolPath (exit $LASTEXITCODE)." }
}
$previousMsys=$env:MSYS
try {
    # Host fixtures retain the symlink rejection cases on Windows.
    $env:MSYS='winsymlinks:lnk'
    $base=@('-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',(Join-Path $projectRoot 'include'))
    $updateExe=Join-Path $testRoot 'app-update-test.exe'
    Invoke-Checked $cxx ($base+@((Join-Path $projectRoot 'tests/app_update_test.cpp'),'-o',$updateExe))
    if (Test-Path -LiteralPath $nro -PathType Leaf) {
        $hash=(Get-FileHash -LiteralPath $nro -Algorithm SHA256).Hash.ToLowerInvariant()
        Invoke-Checked $updateExe @($nro,$hash)
    } else { Invoke-Checked $updateExe @() }
    $networkExe=Join-Path $testRoot 'update-network-test.exe'
    Invoke-Checked $cxx ($base+@((Join-Path $projectRoot 'tests/update_network_test.cpp'),'-o',$networkExe))
    Invoke-Checked $networkExe @()
    Write-Host 'Guardian updater host suites passed. Network access on physical hardware is not covered by these host tests.'
} finally {
    if ($null -eq $previousMsys) { Remove-Item Env:MSYS -ErrorAction SilentlyContinue }
    else { $env:MSYS=$previousMsys }
}
