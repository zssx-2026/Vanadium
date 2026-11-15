<#
.SYNOPSIS
    Builds the Rust shell: shell\target\release\inv.exe.

.DESCRIPTION
    cargo.exe and rustc.exe are not on PATH on this machine, and the active Rust
    toolchain is x86_64-pc-windows-msvc, so link.exe needs the MSVC environment.
    This script resolves both by itself and never touches the C++ kernel build.

.PARAMETER Configuration
    Release (default) or Debug.

.PARAMETER Dist
    Also copy the artifact to <repo>\dist\inv.exe.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File shell\build.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File shell\build.ps1 -Dist
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$Dist
)

$ErrorActionPreference = 'Stop'

$shellDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $shellDir
$manifest = Join-Path $shellDir 'Cargo.toml'

function Find-Cargo {
    $explicit = 'C:\Users\REDMI\.cargo\bin\cargo.exe'
    if (Test-Path $explicit) { return $explicit }
    $found = Get-Command cargo.exe -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    throw 'cargo.exe not found (expected C:\Users\REDMI\.cargo\bin\cargo.exe or PATH).'
}

function Find-VcVars {
    $candidates = @(
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }
    return $null
}

if (-not (Test-Path $manifest)) {
    Write-Host "[build] FAILED: manifest not found: $manifest"
    exit 1
}

$cargo = Find-Cargo
$vcvars = Find-VcVars

$cargoArgs = @('build', '--manifest-path', $manifest)
if ($Configuration -eq 'Release') { $cargoArgs += '--release' }

Write-Host ("[build] cargo   : {0}" -f $cargo)
Write-Host ("[build] vcvars  : {0}" -f $(if ($vcvars) { $vcvars } else { '(not found, using ambient environment)' }))
Write-Host ("[build] command : cargo {0}" -f ($cargoArgs -join ' '))

# cargo writes progress to stderr, and Windows PowerShell 5.1 turns native stderr
# into a terminating error while $ErrorActionPreference is 'Stop'. Capture both
# streams through a file instead, so warnings and errors are reported verbatim.
$logFile = Join-Path ([System.IO.Path]::GetTempPath()) ('inv_build_' + [guid]::NewGuid().ToString('N') + '.log')
$savedEap = $ErrorActionPreference
$ErrorActionPreference = 'SilentlyContinue'
try {
    if ($vcvars) {
        $quoted = ($cargoArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
        $cmdLine = 'call "' + $vcvars + '" >nul 2>&1 && set "CARGO_TERM_COLOR=never" && "' + $cargo + '" ' + $quoted
        cmd /c $cmdLine > $logFile 2>&1
    } else {
        $env:CARGO_TERM_COLOR = 'never'
        & $cargo @cargoArgs > $logFile 2>&1
    }
    $exit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $savedEap
}

if (Test-Path $logFile) {
    Get-Content -LiteralPath $logFile -Encoding UTF8 | ForEach-Object { $_ }
    Remove-Item -LiteralPath $logFile -Force -ErrorAction SilentlyContinue
}
if ($null -eq $exit) { $exit = 1 }

if ($exit -ne 0) {
    Write-Host ("[build] FAILED (cargo exit {0})" -f $exit)
    exit $exit
}

$profileDir = if ($Configuration -eq 'Release') { 'release' } else { 'debug' }
$artifact = Join-Path $shellDir ('target\' + $profileDir + '\inv.exe')
if (-not (Test-Path $artifact)) {
    Write-Host ("[build] FAILED: artifact missing: {0}" -f $artifact)
    exit 1
}

$item = Get-Item $artifact
$hash = (Get-FileHash $artifact -Algorithm SHA256).Hash
Write-Host ("[build] OK {0}  {1:N0} bytes  sha256={2}" -f $item.FullName, $item.Length, $hash)

if ($Dist) {
    $distDir = Join-Path $root 'dist'
    New-Item -ItemType Directory -Force -Path $distDir | Out-Null
    $target = Join-Path $distDir 'inv.exe'
    Copy-Item -LiteralPath $artifact -Destination $target -Force
    Write-Host ("[build] copied -> {0}" -f $target)
}

exit 0
