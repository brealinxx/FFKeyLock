$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$validator = Join-Path $root 'scripts/Test-ReleaseVersion.ps1'
$version = & $validator
$directory = Join-Path $PSScriptRoot ('artifacts/release-tooling-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
foreach ($folder in @('FFKeyLock', 'installer')) {
    New-Item -ItemType Directory -Path (Join-Path $directory $folder) | Out-Null
}
foreach ($file in @('FFKeyLock/Version.h', 'FFKeyLock/FFKeyLock.manifest', 'installer/FFKeyLock.iss', 'CHANGELOG.md', 'CHANGELOG-en.md')) {
    Copy-Item (Join-Path $root $file) (Join-Path $directory $file)
}
$script:checks = 0
function Reject([scriptblock]$Action, [string]$Reason) {
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw "Accepted invalid release: $Reason" }
    $script:checks++
}
& $validator -RepositoryRoot $directory -Tag "v$version" | Out-Null
$script:checks++
Reject { & $validator -RepositoryRoot $directory -Tag 'v999.0.0' } 'tag mismatch'
Reject { & $validator -RepositoryRoot $directory -Tag "$version" } 'missing v prefix'
Reject { & $validator -RepositoryRoot $directory -Artifacts (Join-Path $directory 'missing.exe') } 'missing artifact'

$mutations = @(
    @('FFKeyLock/Version.h', 'VERSION_PATCH \d+', 'VERSION_PATCH 999'),
    @('FFKeyLock/Version.h', 'VERSION_TEXT_W L"[^"]+"', 'VERSION_TEXT_W L"999.0.0"'),
    @('FFKeyLock/Version.h', 'VERSION_TEXT_RC "[^"]+"', 'VERSION_TEXT_RC "999.0.0\0"'),
    @('FFKeyLock/FFKeyLock.manifest', 'assemblyIdentity version="[^"]+"', 'assemblyIdentity version="999.0.0.0"'),
    @('installer/FFKeyLock.iss', '#define AppVersion "[^"]+"', '#define AppVersion "999.0.0"'),
    @('CHANGELOG.md', '## v[^\r\n]+', '## v999.0.0'),
    @('CHANGELOG-en.md', '## v[^\r\n]+', '## v999.0.0')
)
foreach ($mutation in $mutations) {
    $path = Join-Path $directory $mutation[0]
    $original = Get-Content $path -Raw -Encoding UTF8
    Set-Content $path ($original -replace $mutation[1], $mutation[2]) -Encoding UTF8
    Reject { & $validator -RepositoryRoot $directory } $mutation[0]
    Set-Content $path $original -Encoding UTF8
}
Write-Output "PASS: $script:checks release version checks; fixtures: $directory"
