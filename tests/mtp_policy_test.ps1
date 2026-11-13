# Offline tests: only selected path-policy functions are loaded from the AST.
# This never creates Shell.Application or accesses a connected console.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../tools/install-mtp.ps1'))
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Installer syntax error.'}
foreach($name in @('allowedFiles','allowedFolders')){
    $node=$ast.Find({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq ('$'+$name)},$true)
    if(!$node){throw "Missing policy $name"}
    . ([scriptblock]::Create($node.Extent.Text))
}
foreach($name in @('Find-Folder','Find-Leaf','Ensure-Folder','Resolve-Parent','Wait-Folder')){
    $node=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true)
    . ([scriptblock]::Create($node.Extent.Text))
}
$script:checks=0;$script:mutations=0
function Check([bool]$Value){$script:checks++;if(!$Value){throw "MTP policy check $script:checks failed."}}
function Reject([scriptblock]$Call){$failed=$false;try{&$Call|Out-Null}catch{$failed=$true};Check $failed}
function New-MockFolder {
    $folder=[pscustomobject]@{Children=[Collections.Generic.List[object]]::new()}
    $folder | Add-Member ScriptMethod Items {return @($this.Children.ToArray())}
    $folder | Add-Member ScriptMethod NewFolder {param($name) $script:mutations++;Add-MockFolder $this $name | Out-Null}
    return $folder
}
function Add-MockFolder($Parent,[string]$Name){
    $child=New-MockFolder
    $item=[pscustomobject]@{Name=$Name;IsFolder=$true;GetFolder=$child}
    $item|Add-Member ScriptMethod ExtendedProperty {param($key) if($key -eq 'System.FileName'){return $this.Name};return ''}
    $Parent.Children.Add($item)
    return $child
}
function New-MockLeaf([string]$Display,[string]$Full,[string]$Extension){
    $leaf=[pscustomobject]@{Name=$Display;IsFolder=$false;Properties=@{'System.FileName'=$Full;'System.FileExtension'=$Extension}}
    $leaf|Add-Member ScriptMethod ExtendedProperty {param($key) return $this.Properties[$key]}
    return $leaf
}
$sd=New-MockFolder
$switch=Add-MockFolder $sd 'switch';$app=Add-MockFolder $switch 'NinMagicGuardian';$licenses=Add-MockFolder $app 'licencias'
$config=Add-MockFolder $sd 'config';$guardian=Add-MockFolder $config 'NinMagicGuardian';$rescue=Add-MockFolder $guardian 'rescue'
$bootloader=Add-MockFolder $sd 'bootloader';$ini=Add-MockFolder $bootloader 'ini'
$before=$script:mutations
Check ($allowedFiles.Count -eq 14 -and ($allowedFiles|Sort-Object -Unique).Count -eq 14)
foreach($path in $allowedFiles){Check ($null -ne (Resolve-Parent $path))}
Check ($script:mutations -eq $before)
foreach($path in @('atmosphere/package3','bootloader/hekate_ipl.ini','Nintendo/Contents/game.nca','config/NinMagicGuardian/backup/current/manifest.txt','config/NinMagicGuardian/addons/inventory.txt','config/NinMagicGuardian/quarantine/active/plan.txt','config/NinMagicGuardian/app-update/install.txt','../switch/NinMagicGuardian/NinMagic-Guardian.nro','switch/NinMagicGuardian/../other.nro','switch/NinMagicSaves/NinMagic-Saves.nro')){
    Reject {Resolve-Parent $path $true}
}
Check ($script:mutations -eq $before)
$nro='switch/NinMagicGuardian/NinMagic-Guardian.nro'
$app.Children.Add((New-MockLeaf 'NinMagic-Guardian' 'NinMagic-Guardian.nro' '.nro'))
Check ($null -ne (Find-Leaf $app 'NinMagic-Guardian.nro'))
$app.Children.Clear();$app.Children.Add((New-MockLeaf 'NinMagic-Guardian.nro' '' ''))
Check ($null -ne (Find-Leaf $app 'NinMagic-Guardian.nro'))
$app.Children.Clear();$app.Children.Add((New-MockLeaf 'NinMagic-Guardian' '' ''))
Reject {Find-Leaf $app 'NinMagic-Guardian.nro'}
$app.Children.Clear();$app.Children.Add((New-MockLeaf 'NinMagic-Guardian' 'NinMagic-Guardian.txt' '.txt'))
Reject {Find-Leaf $app 'NinMagic-Guardian.nro'}
$app.Children.Clear();$app.Children.Add((New-MockLeaf 'NinMagic-Guardian' 'NinMagic-Guardian.nro' '.nro'));$app.Children.Add((New-MockLeaf 'NinMagic-Guardian.nro' 'NinMagic-Guardian.nro' '.nro'))
Reject {Find-Leaf $app 'NinMagic-Guardian.nro'}
$app.Children.Clear();Add-MockFolder $app 'NinMagic-Guardian.nro'|Out-Null
Reject {Find-Leaf $app 'NinMagic-Guardian.nro'}
$sd=New-MockFolder
Check ($null -eq (Resolve-Parent $nro))
Check ($script:mutations -eq $before)
$created=Resolve-Parent $nro $true
Check ($null -ne $created -and $script:mutations -eq $before+2)
Check ($null -eq (Find-Folder $sd 'config') -and $null -eq (Find-Folder $sd 'bootloader'))
Write-Output "Guardian MTP scope/filename policy: $script:checks offline checks PASS; no device accessed."
