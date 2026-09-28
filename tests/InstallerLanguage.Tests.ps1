[CmdletBinding()]
param([string]$Iscc = 'ISCC.exe')

$ErrorActionPreference = 'Stop'
& $Iscc /Qp "$PSScriptRoot/InstallerLanguage.Tests.iss"
if ($LASTEXITCODE -ne 0) { throw 'Installer language harness build failed.' }
$result = Join-Path $PSScriptRoot ('artifacts/installer-language-' + [guid]::NewGuid().ToString('N') + '.txt')
$exe = Join-Path $PSScriptRoot 'artifacts/InstallerLanguage.Tests.exe'
$process = Start-Process -FilePath $exe -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', ('/ResultFile="' + $result + '"')) -PassThru
if (-not $process.WaitForExit(60000)) {
    Stop-Process -Id $process.Id -Force
    throw 'Installer language harness timed out.'
}
if ($process.ExitCode -ne 0) { throw "Installer language harness failed: $($process.ExitCode)" }
if (-not (Test-Path $result)) { throw 'Installer language harness did not write a result.' }
Get-Content $result
