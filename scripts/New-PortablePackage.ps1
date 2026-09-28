[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('x64', 'x86', 'arm64')][string]$Architecture,
    [string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'dist')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sources = @{ x64 = 'x64/Release/FFKeyLock.exe'; x86 = 'Release/FFKeyLock.exe'; arm64 = 'ARM64/Release/FFKeyLock.exe' }
$exe = Join-Path $root $sources[$Architecture]
$version = & "$PSScriptRoot/Test-ReleaseVersion.ps1" -Artifacts @($exe)
$name = "FFKeyLock-v$version-$Architecture"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
$staging = Join-Path $output ('.staging-' + [guid]::NewGuid().ToString('N'))
$package = Join-Path $staging $name
try {
    New-Item -ItemType Directory -Path $package -Force | Out-Null
    Copy-Item $exe $package
    foreach ($file in @('README.md', 'README.zh-CN.md', 'CHANGELOG.md', 'CHANGELOG-en.md', 'LICENSE')) {
        Copy-Item (Join-Path $root $file) $package
    }
    Copy-Item (Join-Path $root 'FFKeyLock/FFKeyLock.ico') $package
    Copy-Item (Join-Path $root 'packaging/portable.ini') $package
    Copy-Item (Join-Path $root 'Assets') (Join-Path $package 'Assets') -Recurse
    $archive = Join-Path $output "$name.zip"
    Compress-Archive -Path $package -DestinationPath $archive -Force
    $archive
}
finally {
    if (Test-Path $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
}
