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
function Find-Remote([string]$Relative){
    if($Relative.StartsWith('/') -or $Relative.Contains('..') -or $Relative.Contains('\') -or $Relative.Contains(':')){throw 'Unsafe relative source.'}
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
function Copy-Remote([string]$Relative){
    $item=Find-Remote $Relative
    if($null-eq $item){return [pscustomobject]@{Path=$Relative;Present=$false}}
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
$receipts.Add((Copy-Remote 'config/NinMagicGuardian'))
$receipts.Add((Copy-Remote 'switch/NinMagicGuardian/NinMagic-Guardian.nro'))
$manifest=Join-Path $InspectionRoot 'sd-root/config/NinMagicGuardian/backup/current/manifest.txt'
if(Test-Path -LiteralPath $manifest){
    $data=Get-Content -LiteralPath $manifest
    if($data[0]-cne 'NMGUARDIAN1'){throw 'Unrecognized manifest; system files will not be collected.'}
    $paths=@($data | Where-Object { $_.StartsWith('F|') } | ForEach-Object {$_.Split('|',4)[3]})
    if($paths.Count-gt 4096){throw 'Too many manifest entries.'}
    foreach($p in $paths){
        $permitted=$p.StartsWith('atmosphere/') -and !$p.StartsWith('atmosphere/contents/') -or $p.StartsWith('bootloader/') -or $p-ceq 'exosphere.ini' -or $p-ceq 'emuMMC/emummc.ini' -or $p-ceq 'payload.bin' -or $p-ceq 'fusee.bin' -or $p-ceq 'hekate.bin'
        if($p.StartsWith('config/NinMagicGuardian/rescue/')){continue} # already collected above
        if(!$permitted){throw "Source outside the read-only boot-file scope: $p"}
        $receipts.Add((Copy-Remote $p))
    }
}
$receipts | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $InspectionRoot 'readback-receipt.json') -Encoding utf8
Write-Output ('INSPECTION_ROOT='+$InspectionRoot)
Write-Output ('READ_ONLY_SOURCES='+$receipts.Count)
