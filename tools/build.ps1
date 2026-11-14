[CmdletBinding()]
param(
    [string]$DevkitPro = 'C:\devkitPro',
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')][string]$ExpectedVersion='0.1.3'
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stageRoot = [IO.Path]::GetFullPath((Join-Path $env:USERPROFILE '.codex\tmp\ninmagic-guardian-build'))
$safeParent = [IO.Path]::GetFullPath((Join-Path $env:USERPROFILE '.codex\tmp'))
if (-not $stageRoot.StartsWith($safeParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($stageRoot) -ne 'ninmagic-guardian-build') {
    throw 'The build staging folder must be the dedicated Guardian folder inside Codex temporary files.'
}
if ($stageRoot.Contains(' ')) { throw 'devkitPro make requires a staging path without spaces.' }
$bash = Join-Path $DevkitPro 'msys2\usr\bin\bash.exe'
if (-not (Test-Path -LiteralPath $bash -PathType Leaf)) { throw "devkitPro MSYS2 bash not found at $bash." }
foreach ($required in @('source\main.cpp','include','source','assets','romfs','Makefile')) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $required))) { throw "Project is incomplete: $required." }
}
# Only this validated, dedicated temporary directory is cleared. The SD and
# connected console are never build destinations.
if (Test-Path -LiteralPath $stageRoot) {
    if ((Get-Item -LiteralPath $stageRoot -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing a redirected build staging directory.' }
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
$null = New-Item -ItemType Directory -Path $stageRoot -Force
foreach ($name in @('source','include','assets','romfs','Makefile')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $stageRoot -Recurse
}
$posixStage = '/' + $stageRoot.Substring(0,1).ToLowerInvariant() + $stageRoot.Substring(2).Replace('\','/')
$posixDevkit = '/' + $DevkitPro.Substring(0,1).ToLowerInvariant() + $DevkitPro.Substring(2).Replace('\','/')
$jobs = [Math]::Min(8,[Math]::Max(1,[Environment]::ProcessorCount))
$buildCommand = "export DEVKITPRO='$posixDevkit'; export DEVKITA64=`"`$DEVKITPRO/devkitA64`"; export PATH=`"`$DEVKITPRO/tools/bin:`$PATH`"; cd '$posixStage' && make -j$jobs"
& $bash --login -c $buildCommand
if ($LASTEXITCODE -ne 0) { throw "NRO build failed (exit $LASTEXITCODE)." }
$outputFolder = Join-Path $projectRoot 'dist'
$null = New-Item -ItemType Directory -Path $outputFolder -Force
foreach ($extension in @('nro','elf','nacp')) {
    Copy-Item -LiteralPath (Join-Path $stageRoot "NinMagic-Guardian.$extension") -Destination $outputFolder -Force
}
& python (Join-Path $PSScriptRoot 'verify_nro.py') (Join-Path $outputFolder 'NinMagic-Guardian.nro') --expected-version $ExpectedVersion --output (Join-Path $outputFolder 'nro-validation.json')
if ($LASTEXITCODE -ne 0) { throw 'NRO structural and embedded asset verification failed.' }
Write-Host "NRO ready: $(Join-Path $outputFolder 'NinMagic-Guardian.nro')"
