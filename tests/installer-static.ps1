<#
.SYNOPSIS
  Static checks for the inv installer and the PowerShell tooling.

.DESCRIPTION
  Runs without dist\ artifacts:
    - parses every .ps1 under tools\ and tests\
    - checks inv.nsi encoding, required directives and the no-MessageBox rule
    - compiles inv.nsi with -DDRYRUN to prove the script is valid
    - checks license.txt is present
  Prints one PASS/FAIL line per check and ends with STATIC_RESULT=PASS|FAIL.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tests\installer-static.ps1
#>
[CmdletBinding()]
param(
  [string]$Version = ''
)

$ErrorActionPreference = 'Stop'
$script:Root = Split-Path -Parent $PSScriptRoot
$script:Failures = 0
$script:Checks = 0

function Write-Check {
  param([string]$Name, [bool]$Ok, [string]$Detail = '')
  $script:Checks++
  if (-not $Ok) { $script:Failures++ }
  $status = 'FAIL'
  if ($Ok) { $status = 'PASS' }
  $line = 'CHECK ' + $status + ' ' + $Name
  if ($Detail) { $line = $line + ' :: ' + $Detail }
  Write-Host $line
}

function Get-ExpectedVersion {
  if ($Version) { return $Version.Trim() }
  $vf = Join-Path $script:Root 'VERSION'
  if (Test-Path -LiteralPath $vf) {
    $v = (Get-Content -LiteralPath $vf -TotalCount 1)
    if ($v) { return $v.Trim() }
  }
  return '0.1.0'
}

function Get-Makensis {
  $candidates = @()
  $x86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
  if ($x86) { $candidates += (Join-Path $x86 'NSIS\makensis.exe') }
  $pf = [Environment]::GetEnvironmentVariable('ProgramFiles')
  if ($pf) { $candidates += (Join-Path $pf 'NSIS\makensis.exe') }
  $candidates += 'C:\Program Files (x86)\NSIS\makensis.exe'
  foreach ($c in $candidates) {
    if (Test-Path -LiteralPath $c) { return $c }
  }
  return $null
}

$expectedVersion = Get-ExpectedVersion
$nsi = Join-Path $script:Root 'installer\inv.nsi'
$license = Join-Path $script:Root 'installer\license.txt'

# --- 1. PowerShell scripts parse -------------------------------------------
$psFiles = @()
foreach ($dir in @('tools', 'tests', 'installer')) {
  $full = Join-Path $script:Root $dir
  if (Test-Path -LiteralPath $full) {
    $psFiles += @(Get-ChildItem -LiteralPath $full -Filter *.ps1 -File -Recurse)
  }
}
$parseBad = @()
foreach ($f in $psFiles) {
  $tokens = $null; $errors = $null
  $null = [System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$tokens, [ref]$errors)
  if ($errors.Count -gt 0) {
    $parseBad += ($f.Name + '(' + $errors.Count + ' errors: ' + $errors[0].Message + ')')
  }
}
Write-Check 'powershell scripts parse' ($psFiles.Count -gt 0 -and $parseBad.Count -eq 0) ('' + $psFiles.Count + ' file(s); ' + ($parseBad -join '; '))

# --- 2. inv.nsi present and UTF-8 BOM --------------------------------------
$nsiExists = Test-Path -LiteralPath $nsi
Write-Check 'installer/inv.nsi present' $nsiExists $nsi
$bomOk = $false
$nsiText = ''
if ($nsiExists) {
  $bytes = [System.IO.File]::ReadAllBytes($nsi)
  $bomOk = ($bytes.Length -gt 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
  $nsiText = [System.IO.File]::ReadAllText($nsi, [System.Text.Encoding]::UTF8)
}
Write-Check 'inv.nsi has UTF-8 BOM' $bomOk ('required by makensis for the translated LangStrings')

# --- 3. required NSIS directives ------------------------------------------
$required = @(
  'Unicode true',
  'RequestExecutionLevel admin',
  '!include "MUI2.nsh"',
  'MUI_PAGE_COMPONENTS',
  'MUI_UNPAGE_CONFIRM',
  'WriteUninstaller',
  'QuietUninstallString',
  'AddToSystemPath',
  'un.RemoveFromSystemPath',
  'App Paths'
)
$missingDirectives = @()
foreach ($d in $required) {
  if ($nsiText -notmatch [regex]::Escape($d)) { $missingDirectives += $d }
}
Write-Check 'inv.nsi directives' ($nsiExists -and $missingDirectives.Count -eq 0) ('missing: ' + ($missingDirectives -join ', '))

# --- 4. no MessageBox anywhere --------------------------------------------
$msgLines = @()
if ($nsiExists) {
  $lineNo = 0
  foreach ($ln in ($nsiText -split '\r?\n')) {
    $lineNo++
    $t = $ln.Trim()
    if ($t.StartsWith(';') -or $t.StartsWith('#')) { continue }
    if ($t -match '^MessageBox') { $msgLines += ('line ' + $lineNo) }
    if ($t -match '^\s*MessageBox') { $msgLines += ('line ' + $lineNo) }
  }
}
Write-Check 'inv.nsi has no MessageBox' ($msgLines.Count -eq 0) ($msgLines -join ', ')

# --- 5. VIAddVersionKey has no /LANG before MUI_LANGUAGE -------------------
$langBefore = ($nsiText -match 'VIAddVersionKey\s+/LANG=')
Write-Check 'VIAddVersionKey has no early /LANG' (-not $langBefore) 'language files are included after the metadata block'

# --- 6. license.txt -------------------------------------------------------
$licOk = (Test-Path -LiteralPath $license) -and ((Get-Item -LiteralPath $license).Length -gt 200)
Write-Check 'installer/license.txt' $licOk $license

# --- 7. makensis dry run --------------------------------------------------
$makensis = Get-Makensis
Write-Check 'makensis available' ($null -ne $makensis) ('' + $makensis)
if ($makensis -and $nsiExists) {
  $outDir = Join-Path $env:TEMP ('inv-static-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
  New-Item -ItemType Directory -Path $outDir -Force | Out-Null
  $log = Join-Path $outDir 'makensis.log'
  $argLine = '/V2 /DDRYRUN /DOUT_DIR="' + $outDir + '" "' + $nsi + '"'
  $p = Start-Process -FilePath $makensis -ArgumentList $argLine -PassThru -Wait -NoNewWindow -RedirectStandardOutput $log
  $artifact = Join-Path $outDir ('inv-setup-' + $expectedVersion + '.exe')
  $artifactOk = Test-Path -LiteralPath $artifact
  $artifactSize = 0
  if ($artifactOk) { $artifactSize = (Get-Item -LiteralPath $artifact).Length }
  Write-Check 'makensis -DDRYRUN compiles' ($p.ExitCode -eq 0 -and $artifactOk) ('exit=' + $p.ExitCode + ' artifact=' + $artifactSize + ' bytes')
  if ($p.ExitCode -ne 0) {
    $tail = @(Get-Content -LiteralPath $log -ErrorAction SilentlyContinue | Select-Object -Last 12)
    foreach ($t in $tail) { Write-Host ('  nsi| ' + $t) }
  }
  Remove-Item -LiteralPath $outDir -Recurse -Force -ErrorAction SilentlyContinue
}

# --- 8. version metadata and malformed-version rejection -------------------
$metaOk = ($nsiText -match 'VIAddVersionKey\s+"FileVersion"') -and
          ($nsiText -match 'VIAddVersionKey\s+"ProductVersion"') -and
          ($nsiText -match 'VIAddVersionKey\s+"OriginalFilename"')
Write-Check 'inv.nsi version metadata keys' $metaOk 'FileVersion + ProductVersion + OriginalFilename'

if ($makensis -and $nsiExists) {
  $badDir = Join-Path $env:TEMP ('inv-static-bad-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
  New-Item -ItemType Directory -Path $badDir -Force | Out-Null
  $badLog = Join-Path $badDir 'makensis.log'
  $badErr = Join-Path $badDir 'makensis.err'
  $badArg = '/V2 /DDRYRUN /DAPP_VERSION=not-a-version /DOUT_DIR="' + $badDir + '" "' + $nsi + '"'
  # makensis prints compile errors on stderr, so capture both streams
  $bp = Start-Process -FilePath $makensis -ArgumentList $badArg -PassThru -Wait -NoNewWindow -RedirectStandardOutput $badLog -RedirectStandardError $badErr
  $badText = ''
  if (Test-Path -LiteralPath $badLog) { $badText += [System.IO.File]::ReadAllText($badLog) }
  if (Test-Path -LiteralPath $badErr) { $badText += [System.IO.File]::ReadAllText($badErr) }
  $named = ($badText -match 'must be MAJOR\.MINOR\.PATCH')
  Write-Check 'inv.nsi rejects malformed version' (($bp.ExitCode -ne 0) -and $named) ('exit=' + $bp.ExitCode + ' named_message=' + $named)
  Remove-Item -LiteralPath $badDir -Recurse -Force -ErrorAction SilentlyContinue
}

# --- 9. PE export reader baseline -----------------------------------------
$peTest = Join-Path $script:Root 'tests\pe-export-test.ps1'
if (Test-Path -LiteralPath $peTest) {
  $peLog = Join-Path $env:TEMP ('inv-pe-' + [guid]::NewGuid().ToString('N').Substring(0, 8) + '.log')
  $peArg = '-NoProfile -ExecutionPolicy Bypass -File "' + $peTest + '"'
  $pep = Start-Process -FilePath 'powershell' -ArgumentList $peArg -PassThru -Wait -NoNewWindow -RedirectStandardOutput $peLog
  Write-Check 'PE export reader baseline' ($pep.ExitCode -eq 0) ('exit=' + $pep.ExitCode)
  if ($pep.ExitCode -ne 0) {
    Get-Content -LiteralPath $peLog -Tail 8 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host ('  pe| ' + $_) }
  }
  Remove-Item -LiteralPath $peLog -Force -ErrorAction SilentlyContinue
} else {
  Write-Check 'PE export reader baseline' $false 'missing tests\pe-export-test.ps1'
}

# --- summary --------------------------------------------------------------
Write-Host ('checks=' + $script:Checks + ' failures=' + $script:Failures)
if ($script:Failures -gt 0) {
  Write-Host 'STATIC_RESULT=FAIL'
  exit 1
}
Write-Host 'STATIC_RESULT=PASS'
exit 0
