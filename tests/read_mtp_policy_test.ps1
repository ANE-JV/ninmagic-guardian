# Offline policy checks only: no COM, device, local collection or SD writes.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../tools/read-guardian-mtp.ps1'))
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Read-only collector syntax error.'}
foreach($name in @('Test-GuardianSafeRelative','Get-GuardianSourceAction','Get-GuardianManifestSources','Copy-Remote')){
    $node=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name-ceq $name},$true)
    if(!$node){throw "Missing collector policy: $name"}
    . ([scriptblock]::Create($node.Extent.Text))
}
$script:checks=0
function Check([bool]$Value){$script:checks++;if(!$Value){throw "Read-only policy check $script:checks failed."}}
function Reject([scriptblock]$Call){$failed=$false;try{&$Call|Out-Null}catch{$failed=$true};Check $failed}
foreach($path in @('switch/Tools/JKSV.nro','atmosphere/package3','bootloader/ini/User config.ini','config/NinMagicGuardian/rescue/libsys_minerva.bso')){Check (Test-GuardianSafeRelative $path)}
foreach($path in @('','/switch/a.nro','switch/../a.nro','switch//a.nro','switch/a.','switch/a ','switch\a.nro','sdmc:/switch/a.nro','switch/*.nro',('switch/'+('a'*235)+'.nro'),"switch/a`nb.nro",'switch/é.nro')){Check (!(Test-GuardianSafeRelative $path))}
foreach($path in @('atmosphere/package3','atmosphere/config/system_settings.ini','atmosphere/hosts/default.txt','bootloader/ini/User.ini','bootloader/sys/res.pak','atmosphere/kips/emummc.kipm','bootloader/payloads/extra.bin','hekate_ctcaer_6.bin')){Check ((Get-GuardianSourceAction $path 0)-ceq 'CopyFile')}
Check ((Get-GuardianSourceAction 'bootloader/res/logo.png' 1)-ceq 'CopyFile')
Check ((Get-GuardianSourceAction 'config/NinMagicGuardian/rescue/libsys_minerva.bso' 0)-ceq 'AlreadyCollected')
foreach($path in @('switch/JKSV/JKSV.nro','bootloader/res/logo.png','atmosphere/contents/0100000000001000/romfs/file','Nintendo/Contents/game.nca','switch/a/data/file.nro','switch/Ports/game.nro','switch/JKSV/config.ini','switch/a/b/c/d/e/tool.nro','android/image.bin','bootloader/game.nsp','config/NinMagicGuardian/update-url.txt')){
    Reject {Get-GuardianSourceAction $path 0}
}
Reject {Get-GuardianSourceAction 'atmosphere/package3' 4}
function Manifest([string]$Magic,[int]$Mask,[string[]]$Paths){
    @($Magic,'identity=11111111111111111111111111111111','firmware=23.0.1','model=Switch','created=2026-11-14',"options=$Mask",('count='+$Paths.Count),'bytes=0')+@($Paths|ForEach-Object {'F|0|'+('0'*64)+'|'+$_})
}
$v1=@(Get-GuardianManifestSources (Manifest 'NMGUARDIAN1' 0 @('atmosphere/package3','bootloader/hekate_ipl.ini')))
Check ($v1.Count-eq 2 -and @($v1|Where-Object Action -CNE 'CopyFile').Count-eq 0)
Reject {Get-GuardianManifestSources (Manifest 'NMGUARDIAN1' 4 @('switch/JKSV/JKSV.nro'))}
Reject {Get-GuardianManifestSources (Manifest 'NMGUARDIAN2' 0 @('atmosphere/package3'))}
Reject {Get-GuardianManifestSources (Manifest 'NMGUARDIAN1' 0 @('atmosphere/package3','Atmosphere/package3'))}
Reject {Get-GuardianManifestSources ((Manifest 'NMGUARDIAN1' 0 @('atmosphere/package3'))+@('F|bad'))}
Reject {Get-GuardianManifestSources (Manifest 'NMGUARDIAN1' 0 @('switch/JKSV/JKSV.nro'))}
# No file entry can cause a recursive directory collection.
function Find-Remote([string]$Relative){return [pscustomobject]@{IsFolder=$true}}
Reject {Copy-Remote 'atmosphere/package3'}
Reject {Copy-Remote 'switch/NinMagicGuardian/NinMagic-Guardian.nro'}
Write-Output "Guardian read-only manifest V1/system-only policy: $script:checks offline checks PASS; no device accessed."
