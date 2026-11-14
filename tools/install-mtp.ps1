# Read-only inventory by default. Invoke with -Install only after building,
# testing, packaging and publishing. No console UI control, main Hekate config,
# Atmosphere, backup data, inventory, quarantine or game/save access.
[CmdletBinding()]
param(
    [switch]$Install,
    [switch]$Signature,
    [switch]$NroOnly,
    [switch]$WhatIf,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')][string]$ExpectedVersion = '0.2.0',
    [ValidateRange(5,30)][int]$TimeoutSeconds = 30
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Assert-TransferMode([bool]$DoInstall,[bool]$DoSignature,[bool]$OnlyNro) {
    if ($DoInstall -and $DoSignature) { throw 'Elige instalación o firma de solo lectura; no ambas.' }
    if ($OnlyNro -and -not $DoInstall) { throw '-NroOnly requiere -Install y nunca se usa para la firma.' }
}
Assert-TransferMode ([bool]$Install) ([bool]$Signature) ([bool]$NroOnly)
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$channel = 'https://raw.githubusercontent.com/ANE-JV/ninmagic-guardian/main/update.txt'
$validationPath = Join-Path $projectRoot 'dist/validation.json'
$nroPath = Join-Path $projectRoot 'dist/NinMagic-Guardian.nro'
$publicationRoot = Join-Path $projectRoot 'dist/publication'
$packagePath = Join-Path $publicationRoot "versions/v$ExpectedVersion/NinMagic-Guardian-$ExpectedVersion.zip"
$allowedFiles = @(
    'bootloader/ini/ninmagic-guardian.ini',
    'config/NinMagicGuardian/rescue/GPL-2.0.txt',
    'config/NinMagicGuardian/rescue/LEEME.txt',
    'config/NinMagicGuardian/rescue/libsys_minerva.bso',
    'config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin',
    'config/NinMagicGuardian/update-url.txt',
    'switch/NinMagicGuardian/FUENTES.txt',
    'switch/NinMagicGuardian/LEEME.txt',
    'switch/NinMagicGuardian/NinMagic-Guardian.nro',
    'switch/NinMagicGuardian/licencias/CURL-LICENSE.txt',
    'switch/NinMagicGuardian/licencias/FONT-LICENSE.txt',
    'switch/NinMagicGuardian/licencias/GPL-2.0.txt',
    'switch/NinMagicGuardian/licencias/LIBNX-LICENSE.txt',
    'switch/NinMagicGuardian/licencias/ZLIB-LICENSE.txt'
)
$allowedFolders = @('switch','switch/NinMagicGuardian','switch/NinMagicGuardian/licencias',
    'config','config/NinMagicGuardian','config/NinMagicGuardian/rescue','bootloader','bootloader/ini')

function Assert-RegularFile([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "No es un archivo local normal: $Path"
    }
}
function Get-Sha([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Get-BytesSha([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Select-TransferPayload([object[]]$Items,[bool]$OnlyNro) {
    # Selection never weakens full-package validation: even NRO-only transfers
    # require the exact complete 14-file distribution as their source.
    if ($Items.Count -ne $allowedFiles.Count) { throw 'El paquete de origen debe contener los 14 archivos autorizados.' }
    foreach ($required in $allowedFiles) {
        if (@($Items | Where-Object Relative -CEQ $required).Count -ne 1) { throw 'El paquete de origen no coincide con la lista autorizada.' }
    }
    if ($OnlyNro) { return @($Items | Where-Object Relative -CEQ 'switch/NinMagicGuardian/NinMagic-Guardian.nro') }
    return $Items
}

# Validate the exact packaged build before connecting to the MTP namespace.
foreach ($path in @($validationPath,$nroPath,$packagePath)) { Assert-RegularFile $path }
$validation = Get-Content -LiteralPath $validationPath -Raw | ConvertFrom-Json
if ($validation.title -cne 'NinMagic Guardian' -or $validation.version -cne $ExpectedVersion -or
    $validation.manifest -cne $channel -or $validation.package -cne [IO.Path]::GetFileName($packagePath)) {
    throw 'validation.json no identifica esta versión, ZIP y canal oficial. Ejecuta build, tests y package primero.'
}
$expectedNroHash = Get-Sha $nroPath
if ($expectedNroHash -cne $validation.sha256 -or (Get-Item -LiteralPath $nroPath).Length -ne [long]$validation.bytes -or
    (Get-Sha $packagePath) -cne $validation.package_sha256) {
    throw 'El NRO o ZIP no coincide con validation.json. No se transfiere nada.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($packagePath)
$payload = [Collections.Generic.List[object]]::new()
try {
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($entry in $archive.Entries) {
        $name = $entry.FullName
        if (-not $seen.Add($name)) { throw 'El ZIP tiene nombres repetidos.' }
        $allowed = $name -cin $allowedFiles
        if (-not $allowed -or $name.Contains('..') -or $entry.Length -gt 16777216 -or $entry.Length -le 0) {
            throw "Entrada no admitida en el paquete: $name"
        }
        $stream = $entry.Open()
        $memory = [IO.MemoryStream]::new()
        try { $stream.CopyTo($memory); $bytes = $memory.ToArray() }
        finally { $stream.Dispose(); $memory.Dispose() }
        if ($bytes.LongLength -ne $entry.Length) { throw "Entrada incompleta: $name" }
        $payload.Add([pscustomobject]@{Relative=$name;Bytes=$bytes;Size=[long]$bytes.LongLength;Sha256=(Get-BytesSha $bytes);Source=$null})
    }
} finally { $archive.Dispose() }
$actualNames = @($payload.Relative | Sort-Object -CaseSensitive)
$validatedNames = @($validation.entries | Sort-Object -CaseSensitive)
if (($actualNames -join "`n") -cne ($validatedNames -join "`n")) { throw 'El inventario ZIP no coincide con validation.json.' }
foreach ($required in $allowedFiles) {
    if (@($payload | Where-Object Relative -CEQ $required).Count -ne 1) { throw "Falta la entrada obligatoria $required" }
}
$packagedNro = @($payload | Where-Object Relative -CEQ 'switch/NinMagicGuardian/NinMagic-Guardian.nro')[0]
if ($packagedNro.Sha256 -cne $expectedNroHash) { throw 'El NRO del ZIP difiere del NRO validado.' }
$packagedChannel = @($payload | Where-Object Relative -CEQ 'config/NinMagicGuardian/update-url.txt')[0]
if ([Text.Encoding]::ASCII.GetString($packagedChannel.Bytes) -cne ($channel + "`n")) {
    throw 'El canal del ZIP no coincide exactamente con el canal verificado.'
}
$transferPayload = @(Select-TransferPayload $payload.ToArray() ([bool]$NroOnly))

$shell = New-Object -ComObject Shell.Application
$computer = $shell.NameSpace(17)
if ($null -eq $computer) { throw 'No se puede enumerar Este equipo.' }
$devices = @($computer.Items() | Where-Object { $_.IsFolder -and $_.Name -in @('Switch','Nintendo Switch') })
if ($devices.Count -ne 1) { throw 'Debe aparecer exactamente una Switch. Conéctala y abre DBI > Run MTP responder.' }
$sdItems = @($devices[0].GetFolder.Items() | Where-Object { $_.IsFolder -and $_.Name -ceq '1: SD Card' })
if ($sdItems.Count -ne 1) { throw 'No se encuentra una única microSD 1: SD Card de DBI.' }
$sd = $sdItems[0].GetFolder

function Find-Folder($Parent,[string]$Name) {
    $items = @($Parent.Items() | Where-Object { $_.Name -ieq $Name })
    if ($items.Count -gt 1 -or ($items.Count -eq 1 -and -not $items[0].IsFolder)) {
        throw "Destino de carpeta ambiguo: $Name"
    }
    if ($items.Count -eq 1) { return $items[0].GetFolder }
    return $null
}
function Find-Leaf($Parent,[string]$Name) {
    $stem = [IO.Path]::GetFileNameWithoutExtension($Name)
    $extension = [IO.Path]::GetExtension($Name)
    $leafMatches = @($Parent.Items() | Where-Object {
        $fileName = [string]$_.ExtendedProperty('System.FileName')
        $_.Name -ieq $Name -or $fileName -ieq $Name -or $_.Name -ieq $stem
    })
    if ($leafMatches.Count -gt 1 -or ($leafMatches.Count -eq 1 -and $leafMatches[0].IsFolder)) {
        throw "Nombre de archivo ambiguo o usado por carpeta: $Name"
    }
    if ($leafMatches.Count -eq 0) { return $null }
    $knownExtension = $leafMatches[0].ExtendedProperty('System.FileExtension')
    if ($knownExtension -and [string]$knownExtension -ine $extension) {
        throw "La extensión real del destino no coincide: $Name"
    }
    $knownFileName=[string]$leafMatches[0].ExtendedProperty('System.FileName')
    if (-not $knownExtension -and $knownFileName -ine $Name -and $leafMatches[0].Name -ine $Name) {
        throw "MTP no confirma la extensión del archivo existente: $Name"
    }
    return $leafMatches[0]
}
function Remote-Size($Item) {
    $raw = $Item.ExtendedProperty('System.Size')
    if ($null -eq $raw -or [string]::IsNullOrWhiteSpace([string]$raw)) { throw 'MTP no informa un tamaño fiable del archivo.' }
    [long]$size = 0
    if (-not [long]::TryParse([string]$raw,[ref]$size) -or $size -lt 0 -or $size -gt 16777216) {
        throw 'El tamaño del archivo MTP no es válido o supera el límite de esta aplicación.'
    }
    return $size
}
function Wait-Folder($Parent,[string]$Name) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        $found = Find-Folder $Parent $Name
        if ($null -ne $found) { return $found }
        Start-Sleep -Milliseconds 250
    } while ($watch.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    throw "MTP no ha creado la carpeta $Name dentro del plazo. Se detiene sin borrar nada."
}
function Ensure-Folder($Parent,[string]$Name,[string]$Relative) {
    if ($Relative -cnotin $allowedFolders) { throw 'Carpeta fuera del alcance autorizado.' }
    $found = Find-Folder $Parent $Name
    if ($null -ne $found) { return $found }
    [void]$Parent.NewFolder($Name)
    Wait-Folder $Parent $Name
}
function Resolve-Parent([string]$Relative,[bool]$Create=$false) {
    if ($Relative -cnotin $allowedFiles) { throw 'Archivo fuera del alcance autorizado.' }
    $parts=$Relative.Split('/')
    $folder=$sd
    $prefix=''
    for ($i=0;$i -lt $parts.Length-1;$i++) {
        $prefix=if ($prefix) { $prefix+'/'+$parts[$i] } else { $parts[$i] }
        if ($prefix -cnotin $allowedFolders) { throw 'Ruta de carpeta fuera del alcance autorizado.' }
        $folder=if ($Create) { Ensure-Folder $folder $parts[$i] $prefix } else { Find-Folder $folder $parts[$i] }
        if ($null -eq $folder) { return $null }
    }
    return $folder
}
function Wait-Leaf($Parent,[string]$Name,[long]$Size) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $stable = 0
    do {
        $found = Find-Leaf $Parent $Name
        if ($null -ne $found -and (Remote-Size $found) -eq $Size) {
            $stable++
            if ($stable -ge 2) { return $found }
        } else { $stable = 0 }
        Start-Sleep -Milliseconds 250
    } while ($watch.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    throw "No se confirmó la copia MTP de $Name dentro del plazo. Se conservan los archivos anteriores."
}

# Preview visits only the exact application paths. It never enters backup,
# inventory, quarantine, Atmosphere, games, NAND or another application's folder.
# The default preview creates no local files and makes no SD writes.
$preview = [Collections.Generic.List[object]]::new()
foreach ($item in $transferPayload) {
    $parent = Resolve-Parent $item.Relative
    $remote = if ($null -ne $parent) { Find-Leaf $parent ([IO.Path]::GetFileName($item.Relative)) } else { $null }
    $preview.Add([pscustomobject]@{Destination=('sdmc:/'+$item.Relative);Exists=($null -ne $remote);Bytes=$item.Size;Sha256=$item.Sha256})
}
if ((-not $Install -or $WhatIf) -and -not $Signature) {
    [pscustomobject]@{Mode='Inventory only';Version=$ExpectedVersion;Validated=$true;InstallRequested=[bool]$Install;NroOnly=[bool]$NroOnly;WhatIf=[bool]$WhatIf;Files=$preview.ToArray();Instruction='Para copiar estos archivos, ejecuta de nuevo con -Install. No se modificó la consola.'} | ConvertTo-Json -Depth 5
    return
}

# Receipts and two independent copies of prior OWN targets stay in a git-ignored
# local area. Replaced remote leaves are also preserved under unique names.
$ignore = Get-Content -LiteralPath (Join-Path $projectRoot '.gitignore') -Raw
if ($ignore -notmatch '(?m)^private/\r?$') { throw 'private/ debe estar excluido del repositorio antes de respaldar archivos.' }
$privateRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'private'))
if (-not $privateRoot.StartsWith($projectRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Ruta privada fuera del proyecto.' }
if (Test-Path -LiteralPath $privateRoot) {
    $privateItem = Get-Item -LiteralPath $privateRoot -Force
    if (-not $privateItem.PSIsContainer -or ($privateItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'private debe ser una carpeta local normal.' }
} else { $null = New-Item -ItemType Directory -Path $privateRoot }
$receiptRoot = Join-Path $privateRoot ('mtp-'+[guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $receiptRoot
$receipt = [ordered]@{Mode=$(if($Signature){'Read-only app signature'}elseif($NroOnly){'Install NRO only'}else{'Install'});Version=$ExpectedVersion;NroOnly=[bool]$NroOnly;StartedUtc=[DateTime]::UtcNow.ToString('o');Completed=$false;Files=[Collections.Generic.List[object]]::new();Error=$null}
function Save-Receipt {
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $receiptRoot 'receipt.json') -Encoding utf8
}
function Read-Back($Remote,[string]$LocalDir,[long]$ExpectedSize,[string]$ExpectedHash='') {
    $null = New-Item -ItemType Directory -Path $LocalDir
    $namespace = $shell.NameSpace($LocalDir)
    if ($null -eq $namespace) { throw 'Shell no puede abrir la carpeta privada de verificación.' }
    $namespace.CopyHere($Remote,1556)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $lastHash = ''
    $stable = 0
    do {
        $localFiles = @(Get-ChildItem -LiteralPath $LocalDir -Force -File)
        if ($localFiles.Count -gt 1) { throw 'La lectura de vuelta produjo más de un archivo; se detiene.' }
        if ($localFiles.Count -eq 1 -and $localFiles[0].Length -eq $ExpectedSize) {
            $opened = $null
            try {
                # Exclusive access rules out hashing a still-open copy writer.
                $opened = [IO.File]::Open($localFiles[0].FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
                $sha = [Security.Cryptography.SHA256]::Create()
                try { $hash = ([BitConverter]::ToString($sha.ComputeHash($opened))).Replace('-','').ToLowerInvariant() }
                finally { $sha.Dispose() }
                if (($ExpectedHash -eq '' -or $hash -ceq $ExpectedHash) -and $hash -ceq $lastHash) { $stable++ }
                else { $stable = 0 }
                $lastHash = $hash
                if ($stable -ge 1) { return [pscustomobject]@{Path=$localFiles[0].FullName;Sha256=$hash;Bytes=$ExpectedSize} }
            } catch [IO.IOException] { $stable = 0 }
            finally { if ($null -ne $opened) { $opened.Dispose() } }
        }
        Start-Sleep -Milliseconds 250
    } while ($watch.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    throw 'No se pudo verificar la lectura de vuelta MTP. No se borra ni reemplaza ningún respaldo.'
}

if ($Signature) {
    try {
        foreach ($item in ($payload | Sort-Object Relative)) {
            $parent=Resolve-Parent $item.Relative
            $remote=if ($null -ne $parent) { Find-Leaf $parent ([IO.Path]::GetFileName($item.Relative)) } else { $null }
            if ($null -eq $remote) {
                $receipt.Files.Add([ordered]@{Destination=('sdmc:/'+$item.Relative);Present=$false})
                continue
            }
            $size=Remote-Size $remote
            $id=[guid]::NewGuid().ToString('N')
            $first=Read-Back $remote (Join-Path $receiptRoot ('signature-'+$id)) $size
            $second=Read-Back $remote (Join-Path $receiptRoot ('signature-check-'+$id)) $size $first.Sha256
            if ($first.Sha256 -cne $second.Sha256) { throw 'El archivo cambió durante la firma de solo lectura.' }
            $receipt.Files.Add([ordered]@{Destination=('sdmc:/'+$item.Relative);Present=$true;Bytes=$size;Sha256=$first.Sha256;Readback=$first.Path;SecondReadback=$second.Path})
            Save-Receipt
        }
        $receipt.Completed=$true
        $receipt.CompletedUtc=[DateTime]::UtcNow.ToString('o')
        Save-Receipt
        [pscustomobject]@{Mode='Read-only app signature';RemoteWrites=$false;BackupDataAccessed=$false;Files=$receipt.Files;Receipt=(Join-Path $receiptRoot 'receipt.json')} | ConvertTo-Json -Depth 7
        return
    } catch {
        $receipt.Error=$_.Exception.Message
        Save-Receipt
        throw ($_.Exception.Message+' Firma local conservada en: '+$receiptRoot+'. No se modificó la consola.')
    }
}

try {
    $staging = Join-Path $receiptRoot 'incoming'
    $null = New-Item -ItemType Directory -Path $staging
    foreach ($item in $transferPayload) {
        $relativeLocal = $item.Relative.Replace('/',[IO.Path]::DirectorySeparatorChar)
        $item.Source = Join-Path $staging $relativeLocal
        $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($item.Source)) -Force
        [IO.File]::WriteAllBytes($item.Source,$item.Bytes)
        if ((Get-Sha $item.Source) -cne $item.Sha256) { throw 'La copia local de preparación no coincide con el paquete.' }
    }
    Save-Receipt
    # Install the independent resources first and make the menu entry visible
    # last. Never copy directly over an active NRO, payload or configuration.
    $orderedPayload=@($transferPayload | Sort-Object @{Expression={
        if ($_.Relative -ceq 'bootloader/ini/ninmagic-guardian.ini') { 3 }
        elseif ($_.Relative -ceq 'switch/NinMagicGuardian/NinMagic-Guardian.nro') { 2 }
        elseif ($_.Relative.StartsWith('config/NinMagicGuardian/rescue/')) { 0 }
        else { 1 }
    }},Relative)
    # Serial transfers: never issue simultaneous CopyHere to one MTP device.
    foreach ($item in $orderedPayload) {
        $name = [IO.Path]::GetFileName($item.Relative)
        $parent = Resolve-Parent $item.Relative $true
        $record = [ordered]@{Destination=('sdmc:/'+$item.Relative);ExpectedSha256=$item.Sha256;Bytes=$item.Size;Previous=$null;BackupName=$null;StagingName=$null;StagingReadback=$null;Installed=$false;Verified=$false;Readback=$null}
        $receipt.Files.Add($record)
        Save-Receipt
        $old = Find-Leaf $parent $name
        if ($null -ne $old) {
            $oldSize = Remote-Size $old
            $backupId = [guid]::NewGuid().ToString('N')
            $first = Read-Back $old (Join-Path $receiptRoot ("before-$backupId")) $oldSize
            $second = Read-Back $old (Join-Path $receiptRoot ("before-check-$backupId")) $oldSize $first.Sha256
            if ($first.Sha256 -cne $second.Sha256) { throw 'El archivo anterior cambió durante el respaldo.' }
            $record.Previous = [ordered]@{Path=$first.Path;Sha256=$first.Sha256;Bytes=$oldSize;SecondReadback=$second.Path}
            Save-Receipt
            if ($first.Sha256 -ceq $item.Sha256 -and $oldSize -eq $item.Size) {
                $record.Installed=$true; $record.Verified=$true; $record.Readback=$second.Path
                Save-Receipt
                continue
            }
        }
        # Stage a unique leaf and verify it before renaming any existing file.
        $incomingId=[guid]::NewGuid().ToString('N')
        $incomingStem='.'+[IO.Path]::GetFileNameWithoutExtension($name)+'-incoming-'+$incomingId
        $incomingName=$incomingStem+[IO.Path]::GetExtension($name)
        if ($null -ne (Find-Leaf $parent $incomingName)) { throw 'Colisión del archivo temporal; no se modifica.' }
        $transferDir=Join-Path $receiptRoot ('transfer-'+$incomingId)
        $null=New-Item -ItemType Directory -Path $transferDir
        $transferPath=Join-Path $transferDir $incomingName
        [IO.File]::WriteAllBytes($transferPath,$item.Bytes)
        if ((Get-Sha $transferPath) -cne $item.Sha256) { throw 'El temporal local no coincide con el paquete.' }
        $sourceFolder=$shell.NameSpace($transferDir)
        $sourceItem=$sourceFolder.ParseName($incomingName)
        if ($null -eq $sourceItem) { throw 'Shell no puede abrir el archivo temporal preparado.' }
        $record.StagingName=$incomingName
        Save-Receipt
        $parent.CopyHere($sourceItem,1556)
        $incoming=Wait-Leaf $parent $incomingName $item.Size
        $incomingCheck=Read-Back $incoming (Join-Path $receiptRoot ('staged-check-'+$incomingId)) $item.Size $item.Sha256
        $record.StagingReadback=$incomingCheck.Path
        Save-Receipt
        if ($null -ne $old) {
            # Detect any change since the two independent previous-file reads.
            $currentOld=Find-Leaf $parent $name
            if ($null -eq $currentOld -or (Remote-Size $currentOld) -ne $oldSize) { throw 'El destino anterior cambió antes del reemplazo.' }
            $currentCheck=Read-Back $currentOld (Join-Path $receiptRoot ('current-check-'+$backupId)) $oldSize $first.Sha256
            $record.Previous.BeforeRenameReadback=$currentCheck.Path
            $backupStem = '.'+[IO.Path]::GetFileNameWithoutExtension($name)+'-previous-'+$backupId
            $backupName = $backupStem+[IO.Path]::GetExtension($name)
            if ($null -ne (Find-Leaf $parent $backupName)) { throw 'Colisión inesperada de nombre de respaldo; no se modifica.' }
            # Rename only this same FolderItem inside its own existing folder.
            # No MoveHere, Delete, filesystem recursion or NAND/savedata access.
            $renameDisplay = if ($currentOld.Name.EndsWith([IO.Path]::GetExtension($name),[StringComparison]::OrdinalIgnoreCase)) { $backupName } else { $backupStem }
            $record.BackupName=$backupName
            Save-Receipt
            $currentOld.Name=$renameDisplay
            $renamed = Wait-Leaf $parent $backupName $oldSize
            if ($null -ne (Find-Leaf $parent $name)) { throw 'El nombre original sigue ocupado tras renombrarlo. No se sobrescribe.' }
            $renamedCheck = Read-Back $renamed (Join-Path $receiptRoot ("renamed-check-$backupId")) $oldSize $first.Sha256
            $record.Previous.RenamedReadback=$renamedCheck.Path
            Save-Receipt
        }
        if ($null -ne (Find-Leaf $parent $name)) { throw 'El destino apareció antes de copiar; no se sobrescribe.' }
        $promoteDisplay=if ($incoming.Name.EndsWith([IO.Path]::GetExtension($name),[StringComparison]::OrdinalIgnoreCase)) { $name } else { [IO.Path]::GetFileNameWithoutExtension($name) }
        $incoming.Name=$promoteDisplay
        $copied = Wait-Leaf $parent $name $item.Size
        $record.Installed=$true
        Save-Receipt
        $readback = Read-Back $copied (Join-Path $receiptRoot ('installed-check-'+[guid]::NewGuid().ToString('N'))) $item.Size $item.Sha256
        $record.Verified=$true; $record.Readback=$readback.Path
        Save-Receipt
    }
    $receipt.Completed=$true
    $receipt.CompletedUtc=[DateTime]::UtcNow.ToString('o')
    Save-Receipt
    [pscustomobject]@{Installed='sdmc:/switch/NinMagicGuardian/NinMagic-Guardian.nro';Version=$ExpectedVersion;Sha256=$expectedNroHash;Verified=$true;Receipt=(Join-Path $receiptRoot 'receipt.json');Files=$receipt.Files.Count;NroOnly=[bool]$NroOnly;RescueUpdated=(-not $NroOnly);OwnRescueAndMenuEntryUpdated=(-not $NroOnly);MainBootConfigurationChanged=$false;BackupDataAccessed=$false;SavesAccessed=$false} | ConvertTo-Json
} catch {
    $receipt.Error=$_.Exception.Message
    Save-Receipt
    throw ($_.Exception.Message+" Respaldos y registro conservados en: $receiptRoot. No se ha borrado ningún archivo.")
}
