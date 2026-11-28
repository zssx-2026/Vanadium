<#
.SYNOPSIS
  Static checks for the inv installer and the PowerShell tooling.

.DESCRIPTION
  Runs without dist\ artifacts:
    - parses every .ps1 under tools\ and tests\
    - checks inv.nsi encoding, required directives and the no-MessageBox rule
    - compiles inv.nsi with -DDRYRUN to prove the script is valid
    - checks license.txt is present
    - reports the 0.5.0 transport guardrails (no WinHTTP, native imports)
  Prints one PASS/FAIL line per check and ends with STATIC_RESULT=PASS|FAIL.

.PARAMETER RequireNativeTransport
  Enforce the 0.5.0 native-transport guardrails: no WinHTTP references in
  src/include/CMakeLists.txt and a winsock2 + schannel import table. Off by
  default, where both checks print ADVISORY lines instead of failing.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tests\installer-static.ps1

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tests\installer-static.ps1 -RequireNativeTransport
#>
[CmdletBinding()]
param(
  [string]$Version = '',
  [switch]$RequireNativeTransport
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

# Guardrails land before the feature they protect: they always print a line,
# but only count towards STATIC_RESULT when the matching switch is on. That
# keeps the 0.4.x tree green while the 0.5.0 transport rewrite is in flight.
function Write-AdvisoryCheck {
  param([string]$Name, [bool]$Ok, [string]$Detail = '', [string]$Hint = '')
  if ($RequireNativeTransport) { Write-Check $Name $Ok $Detail; return }
  $status = 'FAIL'
  if ($Ok) { $status = 'PASS' }
  $suffix = ''
  if ($Hint) { $suffix = ' (advisory; enable with ' + $Hint + ')' }
  Write-Host ('ADVISORY ' + $status + ' ' + $Name + ' :: ' + $Detail + $suffix)
}

function Get-Objdump {
  $cmd = Get-Command objdump -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  foreach ($p in @('C:\Program Files\mingw64\bin\objdump.exe', 'C:\msys64\mingw64\bin\objdump.exe')) {
    if (Test-Path -LiteralPath $p) { return $p }
  }
  return $null
}

function Read-PeU16 {
  param([byte[]]$B, [int]$O)
  return ([int]$B[$O]) -bor ((([int]$B[$O + 1])) -shl 8)
}

function Read-PeU32 {
  param([byte[]]$B, [int]$O)
  return ([long]$B[$O]) -bor ((([long]$B[$O + 1]) -shl 8)) -bor ((([long]$B[$O + 2]) -shl 16)) -bor ((([long]$B[$O + 3]) -shl 24))
}

# Reads the import directory of a PE image without dumpbin or objdump, so the
# check keeps working on machines that only carry the NSIS toolchain.
function Get-PeImportNames {
  param([string]$Path)
  $B = [System.IO.File]::ReadAllBytes($Path)
  if ($B.Length -lt 0x40) { throw 'file too small for a DOS header' }
  if ($B[0] -ne 0x4D -or $B[1] -ne 0x5A) { throw 'missing MZ signature' }
  $peOff = [int](Read-PeU32 $B 0x3C)
  if (($peOff + 24) -gt $B.Length) { throw 'e_lfanew out of range' }
  if ($B[$peOff] -ne 0x50 -or $B[$peOff + 1] -ne 0x45) { throw 'missing PE signature' }
  $coff = $peOff + 4
  $numSections = [int](Read-PeU16 $B ($coff + 2))
  $optSize = [int](Read-PeU16 $B ($coff + 16))
  $opt = $coff + 20
  $magic = [int](Read-PeU16 $B $opt)
  $ddOff = $opt + 112
  if ($magic -eq 0x10B) { $ddOff = $opt + 96 }
  $impRva = [long](Read-PeU32 $B ($ddOff + 8))
  if ($impRva -eq 0) { return @() }
  $sec = $opt + $optSize
  $sections = @()
  for ($i = 0; $i -lt $numSections; $i++) {
    $s = $sec + ($i * 40)
    $sections += [pscustomobject]@{
      VirtualSize    = [long](Read-PeU32 $B ($s + 8))
      VirtualAddress = [long](Read-PeU32 $B ($s + 12))
      RawSize        = [long](Read-PeU32 $B ($s + 16))
      RawPointer     = [long](Read-PeU32 $B ($s + 20))
    }
  }
  $toOffset = {
    param([long]$Rva)
    foreach ($s in $sections) {
      $size = $s.VirtualSize
      if ($size -eq 0) { $size = $s.RawSize }
      if ($Rva -ge $s.VirtualAddress -and $Rva -lt ($s.VirtualAddress + $size)) {
        return [int]($s.RawPointer + ($Rva - $s.VirtualAddress))
      }
    }
    return -1
  }
  $off = & $toOffset $impRva
  if ($off -lt 0) { throw 'the import directory is not mapped by any section' }
  $names = New-Object System.Collections.ArrayList
  while (($off + 20) -le $B.Length) {
    $thunkRva = [long](Read-PeU32 $B $off)
    $nameRva = [long](Read-PeU32 $B ($off + 12))
    if ($nameRva -eq 0 -and $thunkRva -eq 0) { break }
    $no = & $toOffset $nameRva
    if ($no -lt 0) { break }
    $end = $no
    while ($end -lt $B.Length -and $B[$end] -ne 0) { $end++ }
    if ($end -gt $no) { $null = $names.Add([System.Text.Encoding]::ASCII.GetString($B, $no, $end - $no)) }
    $off += 20
  }
  return @($names)
}

function Get-ExpectedVersion {
  if ($Version) { return $Version.Trim() }
  $vf = Join-Path $script:Root 'VERSION'
  if (Test-Path -LiteralPath $vf) {
    $v = (Get-Content -LiteralPath $vf -TotalCount 1)
    if ($v) { return $v.Trim() }
  }
  return '0.4.0'
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

# --- 10. no WinHTTP left in the sources (0.5.0 transport rewrite) ----------
# The remote layer moves from WinHTTP to the kernel's own winsock2 + Schannel
# stack. Advisory until -RequireNativeTransport is passed.
$winhttpHits = 0
$winhttpWhere = @()
$scanTargets = @()
# Scope is src/, include/ and CMakeLists.txt only: the repo root is never walked,
# so scratch directories at the top level (init/, branches/, objects/) cannot leak
# into the hit count no matter what a stray probe leaves behind.
foreach ($rel in @('src', 'include')) {
  $full = Join-Path $script:Root $rel
  if (Test-Path -LiteralPath $full) {
    $scanTargets += @(Get-ChildItem -LiteralPath $full -Recurse -File -ErrorAction SilentlyContinue)
  }
}
$cmakeFile = Join-Path $script:Root 'CMakeLists.txt'
if (Test-Path -LiteralPath $cmakeFile) { $scanTargets += @(Get-Item -LiteralPath $cmakeFile) }
foreach ($sf in $scanTargets) {
  if ($sf.Length -gt 8MB) { continue }
  $text = ''
  try { $text = [System.IO.File]::ReadAllText($sf.FullName) } catch { continue }
  $n = ([regex]::Matches($text, '(?i)winhttp')).Count
  if ($n -gt 0) {
    $winhttpHits += $n
    $relName = $sf.FullName
    if ($relName.StartsWith($script:Root)) { $relName = $relName.Substring($script:Root.Length).TrimStart('\') }
    $winhttpWhere += ($relName + '(' + $n + ')')
  }
}
Write-AdvisoryCheck 'no WinHTTP references in src/include/CMakeLists.txt' ($winhttpHits -eq 0) ('matches=' + $winhttpHits + ' files=[' + (($winhttpWhere | Select-Object -First 8) -join ', ') + ']') '-RequireNativeTransport'

# --- 11. native transport imports in the built kernel ---------------------
# winsock2 + Schannel replace WinHTTP, so the import table must show WS2_32,
# CRYPT32 and SECUR32 and must no longer name WINHTTP.dll.
$kernelDll = Join-Path $script:Root 'build\cmake\vanadium.dll'
$requiredDlls = @('WS2_32.dll', 'CRYPT32.dll', 'SECUR32.dll')
if (-not (Test-Path -LiteralPath $kernelDll)) {
  Write-AdvisoryCheck 'native transport imports (winsock2 + schannel)' $false ('no kernel build at ' + $kernelDll) '-RequireNativeTransport'
} else {
  $imports = @()
  $importSource = 'none'
  $importErr = ''
  $objdump = Get-Objdump
  if ($objdump) {
    try {
      $raw = (& $objdump -p $kernelDll 2>$null | Out-String)
      $imports = @([regex]::Matches($raw, 'DLL Name:\s*(\S+)') | ForEach-Object { $_.Groups[1].Value })
      if ($imports.Count -gt 0) { $importSource = 'objdump' }
    } catch { $importErr = $_.Exception.Message }
  }
  if ($imports.Count -eq 0) {
    try {
      $imports = @(Get-PeImportNames -Path $kernelDll)
      $importSource = 'pe-parser'
    } catch { $importErr = $_.Exception.Message }
  }
  Write-Host ('  imports| source=' + $importSource + ' ' + ($imports -join ', '))
  $forbidden = @($imports | Where-Object { $_ -match '(?i)^WINHTTP\.dll$' })
  $missing = @()
  foreach ($rd in $requiredDlls) {
    $found = @($imports | Where-Object { $_ -match ('(?i)^' + [regex]::Escape($rd) + '$') })
    if ($found.Count -eq 0) { $missing += $rd }
  }
  $nativeOk = ($imports.Count -gt 0) -and ($forbidden.Count -eq 0) -and ($missing.Count -eq 0)
  $detail = 'source=' + $importSource + ' imports=' + $imports.Count + ' forbidden=[' + ($forbidden -join ',') + '] missing=[' + ($missing -join ',') + ']'
  if ($importErr) { $detail = $detail + ' error=' + $importErr }
  Write-AdvisoryCheck 'native transport imports (winsock2 + schannel)' $nativeOk $detail '-RequireNativeTransport'
}

# --- summary --------------------------------------------------------------
Write-Host ('checks=' + $script:Checks + ' failures=' + $script:Failures)
if ($script:Failures -gt 0) {
  Write-Host 'STATIC_RESULT=FAIL'
  exit 1
}
Write-Host 'STATIC_RESULT=PASS'
exit 0
