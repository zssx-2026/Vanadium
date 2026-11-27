#requires -Version 5.1
<#
  Builds dist\inv-setup-<version>.exe from the payload in -SrcDir with NSIS 3.12.
  Pure CLI, no GUI, no popups.

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File installer\build.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File installer\build.ps1 -DryRun
    powershell -NoProfile -ExecutionPolicy Bypass -File installer\build.ps1 -SrcDir D:\build\payload -OutDir D:\build\out
#>
[CmdletBinding()]
param(
  [string]$Version,
  [string]$SrcDir,
  [string]$OutDir,
  [string]$Makensis,
  [switch]$DryRun,
  [string]$LogDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $SrcDir) { $SrcDir = Join-Path $root 'dist' }
if (-not $OutDir) {
  if ($DryRun) {
    # never leave a payload-less artifact in dist: dry runs go to a scratch directory
    $OutDir = Join-Path $env:TEMP ('inv-dryrun-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
  } else {
    $OutDir = $SrcDir
  }
}
if (-not $LogDir) { $LogDir = Join-Path $root 'reports' }
$nsi = Join-Path $PSScriptRoot 'inv.nsi'

function Write-Log([string]$level, [string]$msg) {
  $line = '[' + (Get-Date -Format 'HH:mm:ss') + '] [' + $level + '] ' + $msg
  Write-Host $line
  Add-Content -LiteralPath $script:logFile -Value $line -Encoding UTF8
}

New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
$script:logFile = Join-Path $LogDir ('build-installer-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
Write-Log INFO ('repo      : ' + $root)
Write-Log INFO ('payload   : ' + $SrcDir)
Write-Log INFO ('output    : ' + $OutDir)
Write-Log INFO ('script    : ' + $nsi)

if (-not (Test-Path -LiteralPath $nsi)) { Write-Log FAIL ('missing script ' + $nsi); Write-Host 'BUILD_RESULT=FAIL'; exit 2 }

if (-not $Version) {
  $vf = Join-Path $root 'VERSION'
  if (Test-Path -LiteralPath $vf) {
    $Version = (Get-Content -LiteralPath $vf -TotalCount 1).Trim()
  }
}
if (-not $Version) { $Version = '0.1.0' }
if ($Version -notmatch '^\d+\.\d+\.\d+(-[A-Za-z0-9.]+)?$') {
    Write-Log FAIL ('version must be MAJOR.MINOR.PATCH[-suffix], got "' + $Version + '"')
  Write-Host 'BUILD_RESULT=FAIL'
  exit 2
}
Write-Log INFO ('version   : ' + $Version)

if (-not $Makensis) {
  $candidates = @(
    'C:\Program Files (x86)\NSIS\makensis.exe',
    'C:\Program Files\NSIS\makensis.exe'
  )
  foreach ($c in $candidates) { if (Test-Path -LiteralPath $c) { $Makensis = $c; break } }
}
if (-not $Makensis -or -not (Test-Path -LiteralPath $Makensis)) {
  Write-Log FAIL 'makensis.exe not found; pass -Makensis <path>'
  Write-Host 'BUILD_RESULT=FAIL'
  exit 2
}
Write-Log INFO ('makensis  : ' + $Makensis)

if (-not $DryRun) {
  $payload = @(
    @{ Path = (Join-Path $SrcDir 'inv.exe');                   Kind = 'file' },
    @{ Path = (Join-Path $SrcDir 'vanadium.dll');              Kind = 'file' },
    @{ Path = (Join-Path $SrcDir 'java\vanadium-report.jar');  Kind = 'file' },
    @{ Path = (Join-Path $SrcDir 'java\runtime');              Kind = 'dir'  },
    @{ Path = (Join-Path $SrcDir 'docs');                      Kind = 'dir'  }
  )
  $missing = @()
  foreach ($item in $payload) {
    if ($item.Kind -eq 'file') {
      if (-not (Test-Path -LiteralPath $item.Path -PathType Leaf)) { $missing += $item.Path }
    } else {
      if (-not (Test-Path -LiteralPath $item.Path -PathType Container)) { $missing += ($item.Path + '\') }
    }
  }
  if ($missing.Count -gt 0) {
    foreach ($m in $missing) { Write-Log FAIL ('missing payload ' + $m) }
    Write-Log INFO 'build the application first; no artifact was produced'
    Write-Host 'BUILD_RESULT=FAIL'
    exit 4
  }
  Write-Log INFO 'payload complete'
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$expected = Join-Path $OutDir ('inv-setup-' + $Version + '.exe')
if (Test-Path -LiteralPath $expected) { Remove-Item -LiteralPath $expected -Force }

$switches = '/V2'
if ($DryRun) { $switches += ' /DDRYRUN' }
$argLine = $switches + ' /DAPP_VERSION=' + $Version + ' /DSRC_DIR="' + $SrcDir + '" /DOUT_DIR="' + $OutDir + '" "' + $nsi + '"'
Write-Log INFO ('makensis ' + $argLine)

$mkLog = Join-Path $LogDir ('makensis-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
$proc = Start-Process -FilePath $Makensis -ArgumentList $argLine -PassThru -Wait -NoNewWindow -RedirectStandardOutput $mkLog
Write-Log INFO ('makensis exit=' + $proc.ExitCode)
if ($proc.ExitCode -ne 0) {
  Write-Log FAIL 'makensis failed; tail of compiler output:'
  Get-Content -LiteralPath $mkLog -Tail 25 | ForEach-Object { Write-Log FAIL ('  ' + $_) }
  Write-Host 'BUILD_RESULT=FAIL'
  exit 1
}

if (-not (Test-Path -LiteralPath $expected)) {
  Write-Log FAIL ('expected artifact not produced: ' + $expected)
  Write-Host 'BUILD_RESULT=FAIL'
  exit 1
}

$fi = Get-Item -LiteralPath $expected
$sha = (Get-FileHash -LiteralPath $expected -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Log INFO ('artifact  : ' + $fi.FullName)
Write-Log INFO ('size      : ' + $fi.Length + ' bytes')
Write-Log INFO ('sha256    : ' + $sha)
Write-Log INFO ('compiler  : ' + $mkLog)
Write-Host ('BUILD_RESULT=PASS version=' + $Version + ' size=' + $fi.Length + ' sha256=' + $sha)
exit 0
