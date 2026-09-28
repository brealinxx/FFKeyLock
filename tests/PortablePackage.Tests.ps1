[CmdletBinding()]
param([Parameter(Mandatory)][string]$Archive)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$version = & "$root/scripts/Test-ReleaseVersion.ps1"
$directory = Join-Path $PSScriptRoot ('artifacts/portable-' + [guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $Archive -DestinationPath $directory
$folders = @(Get-ChildItem $directory -Directory)
if ($folders.Count -ne 1) { throw 'ZIP must contain one package directory.' }
$package = $folders[0].FullName
foreach ($file in @('FFKeyLock.exe', 'FFKeyLock.ico', 'portable.ini', 'README.md', 'README.zh-CN.md', 'CHANGELOG.md', 'CHANGELOG-en.md', 'LICENSE', 'Assets/intro_cn.png', 'Assets/intro_en.png')) {
    if (-not (Test-Path (Join-Path $package $file) -PathType Leaf)) { throw "Missing package file: $file" }
}
& "$root/scripts/Test-ReleaseVersion.ps1" -Artifacts @((Join-Path $package 'FFKeyLock.exe')) | Out-Null
if ((Get-FileHash (Join-Path $package 'portable.ini')).Hash -ne (Get-FileHash "$root/packaging/portable.ini").Hash) {
    throw 'Portable configuration differs from the shipped template.'
}
$config = Get-Content (Join-Path $package 'portable.ini') -Raw
if ($config -notmatch '(?m)^Version=2\s*$' -or $config -notmatch '(?m)^Count=0\s*$') {
    throw 'Portable template must be a valid empty version-2 library.'
}
if (@(Get-ChildItem $package -Recurse -File | Where-Object { $_.Extension -in @('.pdb', '.obj', '.tlog', '.bak') }).Count) {
    throw 'Generated or personal files leaked into the portable package.'
}
Write-Output "PASS: portable ZIP contents and version $version; extracted: $package"
