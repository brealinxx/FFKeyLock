[CmdletBinding()]
param(
    [string]$Tag,
    [string[]]$Artifacts = @(),
    [string]$RepositoryRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = 'Stop'
function Require-Match([string]$Text, [string]$Pattern, [string]$Label) {
    $match = [regex]::Match($Text, $Pattern)
    if (-not $match.Success) { throw "Missing or invalid $Label." }
    return $match.Groups[1].Value
}
function Require-Equal([string]$Actual, [string]$Expected, [string]$Label) {
    if ($Actual -cne $Expected) { throw "$Label is '$Actual'; expected '$Expected'." }
}

$header = Get-Content (Join-Path $RepositoryRoot 'FFKeyLock/Version.h') -Raw
$parts = foreach ($part in @('MAJOR', 'MINOR', 'PATCH')) {
    Require-Match $header "(?m)^#define FFKEYLOCK_VERSION_$part ([0-9]+)\s*$" $part
}
$version = $parts -join '.'
Require-Equal (Require-Match $header '(?m)^#define FFKEYLOCK_VERSION_TEXT "([^"]+)"' 'version text') $version 'Version text'
Require-Equal (Require-Match $header '(?m)^#define FFKEYLOCK_VERSION_TEXT_W L"([^"]+)"' 'wide version text') $version 'Wide version text'
Require-Equal (Require-Match $header '(?m)^#define FFKEYLOCK_VERSION_TEXT_RC "([^"]+)\\0"' 'resource version text') $version 'Resource version text'
[xml]$manifest = Get-Content (Join-Path $RepositoryRoot 'FFKeyLock/FFKeyLock.manifest') -Raw
Require-Equal $manifest.assembly.assemblyIdentity.version "$version.0" 'Manifest version'
$installer = Get-Content (Join-Path $RepositoryRoot 'installer/FFKeyLock.iss') -Raw
Require-Equal (Require-Match $installer '(?m)^#define AppVersion "([^"]+)"' 'installer version') $version 'Installer version'
foreach ($file in @('CHANGELOG.md', 'CHANGELOG-en.md')) {
    $text = Get-Content (Join-Path $RepositoryRoot $file) -Raw -Encoding UTF8
    Require-Equal (Require-Match $text '(?m)^## v([^\r\n]+)' $file) $version $file
}
if ($Tag) { Require-Equal $Tag "v$version" 'Release tag' }
foreach ($artifact in $Artifacts) {
    $info = [System.Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path $artifact).Path)
    $fileVersion = '{0}.{1}.{2}.{3}' -f $info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart
    $productVersion = '{0}.{1}.{2}.{3}' -f $info.ProductMajorPart, $info.ProductMinorPart, $info.ProductBuildPart, $info.ProductPrivatePart
    Require-Equal $fileVersion "$version.0" "$artifact file version"
    Require-Equal $productVersion "$version.0" "$artifact product version"
}
$version
