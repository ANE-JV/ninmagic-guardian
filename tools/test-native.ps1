[CmdletBinding()]
param([string]$DevkitPro='C:\devkitPro')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testRoot=[IO.Path]::GetFullPath((Join-Path $env:USERPROFILE '.codex/tmp/ninmagic-guardian-native-tests'))
$null=New-Item -ItemType Directory -Path $testRoot -Force
$cc=Join-Path $DevkitPro 'msys2/usr/bin/gcc.exe'
$cxx=Join-Path $DevkitPro 'msys2/usr/bin/g++.exe'
function Invoke-Checked([string]$ToolPath,[string[]]$ToolArguments) {
    $normalized=@($ToolArguments | ForEach-Object { $_.Replace('\','/') })
    & $ToolPath @normalized
    if ($LASTEXITCODE -ne 0) { throw "Native adapter test failed (exit $LASTEXITCODE)." }
}
$previousMsys=$env:MSYS
try {
    $env:MSYS='winsymlinks:lnk'
    $include=Join-Path $projectRoot 'include'
    $objects=@()
    foreach ($source in @('guardian_core','guardian_sha256')) {
        $object=Join-Path $testRoot ($source+'.o')
        Invoke-Checked $cc @('-std=c11','-O2','-Wall','-Wextra','-Werror','-I',$include,'-c',(Join-Path $projectRoot ('source/'+$source+'.c')),'-o',$object)
        $objects+=$object
    }
    $test=Join-Path $testRoot 'native-fs-test.exe'
    Invoke-Checked $cxx (@('-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',$include,(Join-Path $projectRoot 'tests/native_fs_test.cpp'))+$objects+@('-o',$test))
    Invoke-Checked $test @()
    $rescueTest=Join-Path $testRoot 'rescue-install-test.exe'
    Invoke-Checked $cxx (@('-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',$include,(Join-Path $projectRoot 'tests/rescue_install_test.cpp'))+$objects+@('-o',$rescueTest))
    Invoke-Checked $rescueTest @()
} finally {
    if ($null -eq $previousMsys) { Remove-Item Env:MSYS -ErrorAction SilentlyContinue }
    else { $env:MSYS=$previousMsys }
}
