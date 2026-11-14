# Read-only collection. Never copies to, renames or removes anything on the SD.
[CmdletBinding()]
param([string]$InspectionRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$InspectionRoot){$InspectionRoot=Join-Path $project ('private/inspection-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
$InspectionRoot=[IO.Path]::GetFullPath($InspectionRoot)
$allowed=[IO.Path]::GetFullPath((Join-Path $project 'private'))
if(!$InspectionRoot.StartsWith($allowed+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'The local inspection must be inside Guardian/private.'}
$null=New-Item -ItemType Directory -Path $InspectionRoot -Force
. (Join-Path $PSScriptRoot 'mtp-file-ops.ps1')
$sh=New-Object -ComObject Shell.Application
$devices=@($sh.NameSpace(17).Items() | Where-Object {$_.Name -in @('Switch','Nintendo Switch')})
if($devices.Count-ne 1){throw 'Exactly one DBI Switch must be connected.'}
$cards=@($devices[0].GetFolder.Items() | Where-Object {$_.Name-ceq '1: SD Card' -and $_.IsFolder})
if($cards.Count-ne 1){throw 'DBI SD Card was not found.'}
$sd=$cards[0].GetFolder
function Test-GuardianSafeRelative([string]$Relative){
    if([string]::IsNullOrEmpty($Relative) -or $Relative.Length-gt 240 -or $Relative[0]-eq '/' -or $Relative.EndsWith('/') -or $Relative-match '[^\x20-\x7e]' -or $Relative.IndexOfAny([char[]]'\:|*?"<>')-ge 0){return $false}
    foreach($part in $Relative.Split('/')){if(!$part -or $part-in @('.','..') -or $part.EndsWith('.') -or $part.EndsWith(' ')){return $false}}
    return $true
}
function Get-GuardianSourceAction([string]$Relative,[uint32]$Options){
    if(!(Test-GuardianSafeRelative $Relative) -or $Options-gt 3){throw 'Unsafe or unsupported manifest source.'}
    $p=$Relative.ToLowerInvariant()
    if($p-in @('config/ninmagicguardian/rescue/ninmagic-guardian-rescue.bin','config/ninmagicguardian/rescue/libsys_minerva.bso')){return 'AlreadyCollected'}
    if($p-in @('atmosphere/package3','atmosphere/fusee-secondary.bin','atmosphere/fusee-mtc.bin','atmosphere/reboot_payload.bin','atmosphere/secmon.bin','atmosphere/warmboot.bin','atmosphere/fatal.bin','exosphere.ini','emummc/emummc.ini','bootloader/hekate_ipl.ini','bootloader/nyx.ini','bootloader/patches.ini','bootloader/update.bin','payload.bin','fusee.bin','hekate.bin')){return 'CopyFile'}
    if(($p.StartsWith('atmosphere/config/') -or $p.StartsWith('atmosphere/config_templates/') -or $p.StartsWith('bootloader/ini/')) -and $p.EndsWith('.ini')){return 'CopyFile'}
    if($p.StartsWith('atmosphere/hosts/') -and $p.EndsWith('.txt')){return 'CopyFile'}
    if($p.StartsWith('bootloader/sys/') -and @('.bso','.bin','.pak','.kipm')-contains [IO.Path]::GetExtension($p)){return 'CopyFile'}
    if($p.StartsWith('atmosphere/kips/') -and @('.kip','.kipm')-contains [IO.Path]::GetExtension($p)){return 'CopyFile'}
    if($p.StartsWith('bootloader/payloads/') -and $p.EndsWith('.bin')){return 'CopyFile'}
    if(!$p.Contains('/') -and $p.StartsWith('hekate_ctcaer_') -and $p.EndsWith('.bin')){return 'CopyFile'}
    if(($Options-band 1)-ne 0 -and ($p-eq 'bootloader/bootlogo.bmp' -or ($p.StartsWith('bootloader/res/') -and @('.bmp','.jpg','.png')-contains [IO.Path]::GetExtension($p)))){return 'CopyFile'}
    throw "Source outside the read-only boot-file scope: $Relative"
}
function Get-GuardianManifestSources([string[]]$Data){
    if($Data.Count-lt 8 -or $Data[0]-cne 'NMGUARDIAN1'){throw 'Unrecognized manifest; live sources will not be collected.'}
    if($Data[5]-cnotmatch '^options=([0-3])$'){throw 'Invalid manifest options.'}
    [uint32]$options=[uint32]$Matches[1]
    if($Data[6]-cnotmatch '^count=([0-9]{1,4})$'){throw 'Invalid manifest entry count.'}
    $count=[int]$Matches[1]
    if($count-lt 1 -or $count-gt 4096 -or $Data.Count-ne $count+8){throw 'Incomplete or oversized manifest.'}
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    for($i=8;$i-lt $Data.Count;$i++){
        if($Data[$i]-cnotmatch '^F\|[0-9]{1,20}\|[0-9a-fA-F]{64}\|(.+)$'){throw 'Invalid manifest file entry.'}
        $path=$Matches[1]
        if(!$seen.Add($path)){throw 'Duplicate manifest source.'}
        [pscustomobject]@{Path=$path;Action=(Get-GuardianSourceAction $path $options)}
    }
}
function Find-Remote([string]$Relative){
    if(!(Test-GuardianSafeRelative $Relative)){throw 'Unsafe relative source.'}
    if($Relative.StartsWith('switch/',[StringComparison]::OrdinalIgnoreCase)){throw 'switch is entirely outside Guardian system collection.'}
    $parts=$Relative.Split('/');$folder=$sd;$item=$null
    for($i=0;$i-lt $parts.Count;$i++){
        $part=$parts[$i]
        $matches=@($folder.Items() | Where-Object { $_.Name-ieq $part -or $_.ExtendedProperty('System.FileName')-ieq $part })
        if($matches.Count-eq 0){return $null}
        if($matches.Count-ne 1){throw "Ambiguous remote source: $Relative"}
        $item=$matches[0]
        if($i-lt $parts.Count-1){if(!$item.IsFolder){throw "Remote parent is not a folder: $Relative"};$folder=$item.GetFolder}
    }
    return $item
}
function Copy-Remote([string]$Relative,[bool]$AllowFolder=$false){
    $item=Find-Remote $Relative
    if($null-eq $item){return [pscustomobject]@{Path=$Relative;Present=$false}}
    if($item.IsFolder -and !$AllowFolder){throw "A live file source is a directory; no recursive copy is permitted: $Relative"}
    $dest=Join-Path (Join-Path $InspectionRoot 'sd-root') ($Relative.Replace('/',[IO.Path]::DirectorySeparatorChar))
    $parent=Split-Path -Parent $dest
    $null=New-Item -ItemType Directory -Path $parent -Force
    if(Test-Path -LiteralPath $dest){throw "Local collection destination already exists: $dest"}
    $result=[NinMagicMtpOps]::CopyChecked($item,$sh.NameSpace($parent).Self)
    if(!(Test-Path -LiteralPath $dest)){throw "The requested local readback was not created: $Relative"}
    $record=[ordered]@{Path=$Relative;Present=$true;Folder=[bool]$item.IsFolder;CopyCallbacks=$result.CopyCallbacks}
    if(!$item.IsFolder){$record.Bytes=(Get-Item -LiteralPath $dest).Length;$record.Sha256=(Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()}
    return [pscustomobject]$record
}
$receipts=[Collections.Generic.List[object]]::new()
$receipts.Add((Copy-Remote 'config/NinMagicGuardian' $true))
$manifest=Join-Path $InspectionRoot 'sd-root/config/NinMagicGuardian/backup/current/manifest.txt'
if(Test-Path -LiteralPath $manifest){
    $sources=@(Get-GuardianManifestSources @(Get-Content -LiteralPath $manifest))
    foreach($source in $sources){
        if($source.Action-ceq 'CopyFile'){$receipts.Add((Copy-Remote $source.Path))}
        else{$receipts.Add([pscustomobject]@{Path=$source.Path;SkippedLiveSource=$true;Reason=$source.Action})}
    }
}
$receipts | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $InspectionRoot 'readback-receipt.json') -Encoding utf8
Write-Output ('INSPECTION_ROOT='+$InspectionRoot)
Write-Output ('READ_ONLY_SOURCES='+$receipts.Count)
