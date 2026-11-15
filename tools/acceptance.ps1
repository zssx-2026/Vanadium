<#
.SYNOPSIS
  Ten-step acceptance run for the inv installer (CLI only, no GUI).

.DESCRIPTION
  Installs dist\inv-setup-<version>.exe silently, exercises the CLI, rolls a
  snapshot back, verifies, and uninstalls silently. Every step prints
  PASS/FAIL (plus WARN for non-blocking observations) and the full run is
  teed to reports\acceptance-<timestamp>.log. Failing steps always print the
  captured stdout/stderr so the log is self-contained.

.PARAMETER SetupPath
  Installer to test. Defaults to <repo>\dist\inv-setup-<version>.exe.

.PARAMETER Version
  Expected product version. Defaults to the first line of <repo>\VERSION,
  then to 0.1.0.

.PARAMETER LogDir
  Directory for the acceptance log. Defaults to <repo>\reports.

.PARAMETER TimeoutSeconds
  Per-command timeout. Defaults to 120.

.PARAMETER KeepTemp
  Keep the temporary repository under %TEMP% for inspection.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\acceptance.ps1
#>
[CmdletBinding()]
param(
  [string]$SetupPath = '',
  [string]$Version = '',
  [string]$LogDir = '',
  [int]$TimeoutSeconds = 120,
  [switch]$KeepTemp
)

$ErrorActionPreference = 'Stop'
$script:Root = Split-Path -Parent $PSScriptRoot
$script:InstallDir = Join-Path $env:ProgramFiles 'Infinity\inv'
$script:InvExe = Join-Path $script:InstallDir 'inv.exe'
$script:UninstallExe = Join-Path $script:InstallDir 'uninstall.exe'
$script:UninstallKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\inv'
$script:AppPathKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\inv.exe'
$script:EnvKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'

$script:Commands = @(
  'help','version','set','init','status','push','pull','log','diff','reg','unreg',
  'list','search','rb','rm','clone','fork','login','release','edit','download',
  'verify','gc','doctor','report','remote','self-test','bench','completion'
)

# ---------------------------------------------------------------------------
# logging / bookkeeping
# ---------------------------------------------------------------------------
if (-not $LogDir) { $LogDir = Join-Path $script:Root 'reports' }
New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:LogPath = Join-Path $LogDir ('acceptance-' + $stamp + '.log')
Set-Content -Path $script:LogPath -Value ('inv acceptance run ' + (Get-Date -Format o)) -Encoding UTF8

$script:Results = New-Object System.Collections.ArrayList

function Write-Log {
  param([string]$Text, [string]$Level = 'INFO')
  $line = '[' + (Get-Date -Format 'HH:mm:ss') + '] [' + $Level + '] ' + $Text
  Write-Host $line
  Add-Content -Path $script:LogPath -Value $line -Encoding UTF8
}

function Add-Result {
  param([int]$Step, [string]$Name, [string]$Status, [string]$Detail = '')
  $null = $script:Results.Add([pscustomobject]@{
    Step = $Step; Name = $Name; Status = $Status; Detail = $Detail
  })
  $lvl = 'INFO'
  if ($Status -eq 'FAIL') { $lvl = 'FAIL' }
  if ($Status -eq 'WARN') { $lvl = 'WARN' }
  $msg = 'step ' + $Step + ' ' + $Status + ' - ' + $Name
  if ($Detail) { $msg = $msg + ' :: ' + $Detail }
  Write-Log $msg $lvl
}

function Show-CliOutput {
  param([string]$Label, [object]$Result)
  if ($null -eq $Result) { return }
  Write-Log ($Label + ' exit=' + $Result.ExitCode + ' timedOut=' + $Result.TimedOut)
  if ($Result.StdOut) {
    foreach ($ln in ($Result.StdOut -split '\r?\n')) {
      if ($ln -ne '') { Write-Log ('  out| ' + $ln) }
    }
  }
  if ($Result.StdErr) {
    foreach ($ln in ($Result.StdErr -split '\r?\n')) {
      if ($ln -ne '') { Write-Log ('  err| ' + $ln) }
    }
  }
}

# ---------------------------------------------------------------------------
# process helpers
# ---------------------------------------------------------------------------
function Invoke-Cli {
  param(
    [string]$Exe,
    [string]$Arguments = '',
    [string]$WorkingDirectory = '',
    [int]$Timeout = 0
  )
  if ($Timeout -le 0) { $Timeout = $TimeoutSeconds }
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $Exe
  $psi.Arguments = $Arguments
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.RedirectStandardInput = $true
  $psi.CreateNoWindow = $true
  if ($WorkingDirectory) { $psi.WorkingDirectory = $WorkingDirectory }
  $p = New-Object System.Diagnostics.Process
  $p.StartInfo = $psi
  try {
    $null = $p.Start()
  } catch {
    return [pscustomobject]@{ ExitCode = -2; StdOut = ''; StdErr = $_.Exception.Message; TimedOut = $false }
  }
  $p.StandardInput.Close()
  $outTask = $p.StandardOutput.ReadToEndAsync()
  $errTask = $p.StandardError.ReadToEndAsync()
  $exited = $p.WaitForExit($Timeout * 1000)
  if (-not $exited) {
    try { $p.Kill() } catch { }
    $null = $p.WaitForExit(5000)
    $o = ''; $e = ''
    try { $o = $outTask.Result } catch { }
    try { $e = $errTask.Result } catch { }
    return [pscustomobject]@{ ExitCode = -1; StdOut = $o; StdErr = $e; TimedOut = $true }
  }
  $p.WaitForExit()
  return [pscustomobject]@{
    ExitCode = $p.ExitCode; StdOut = $outTask.Result; StdErr = $errTask.Result; TimedOut = $false
  }
}

function Invoke-Inv {
  param([string]$Arguments, [string]$WorkingDirectory = '', [int]$Timeout = 0)
  # the shell owns --lang/--json; the kernel strips -no-color/-json/-yes itself.
  # do NOT append -lang here: it is not consumed by the kernel and would leak into
  # positional arguments (e.g. "set max_branch -lang" would store the flag as a value).
  # the shell's own --lang is ignored too: dispatch() reloads the language from the
  # stored setting on every run, so the catalog language is pinned with "set lang".
  $full = $Arguments + ' -no-color'
  return Invoke-Cli -Exe $script:InvExe -Arguments $full -WorkingDirectory $WorkingDirectory -Timeout $Timeout
}

function Invoke-SetupExe {
  param([string]$Exe, [string]$Arguments, [int]$Timeout = 0)
  if ($Timeout -le 0) { $Timeout = 300 }
  $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru -Wait
  return $p.ExitCode
}

function Get-JsonProp {
  param($Object, [string]$Name)
  if ($null -eq $Object) { return $null }
  if ($Object -is [System.Array]) { return $null }
  foreach ($p in $Object.PSObject.Properties) {
    if ($p.Name -ieq $Name) { return $p.Value }
  }
  return $null
}

function ConvertFrom-JsonSafe {
  param([string]$Text)
  if (-not $Text) { return $null }
  $t = $Text.Trim()
  if (-not $t) { return $null }
  try { return (ConvertFrom-Json -InputObject $t) } catch { return $null }
}

# ---------------------------------------------------------------------------
# PE export reader (step 4: prove vanadium.dll is a real PE with exports)
# ---------------------------------------------------------------------------
function Convert-RvaToOffset {
  param([object[]]$Sections, [uint32]$Rva)
  foreach ($s in $Sections) {
    if (($Rva -ge $s.VAddr) -and ($Rva -lt ($s.VAddr + $s.VSize))) {
      return [int64]($Rva - $s.VAddr + $s.RAddr)
    }
  }
  return -1
}

function Get-PeExportNames {
  param([string]$Path)
  $fs = [System.IO.File]::OpenRead($Path)
  $br = New-Object System.IO.BinaryReader($fs)
  try {
    if ($br.ReadUInt16() -ne 0x5A4D) { throw 'missing MZ header' }
    $fs.Position = 0x3C
    $peOff = $br.ReadUInt32()
    $fs.Position = $peOff
    if ($br.ReadUInt32() -ne 0x00004550) { throw 'missing PE signature' }
    $null = $br.ReadUInt16()
    $numSections = $br.ReadUInt16()
    $null = $br.ReadUInt32(); $null = $br.ReadUInt32(); $null = $br.ReadUInt32()
    $optSize = $br.ReadUInt16()
    $null = $br.ReadUInt16()
    $optOff = $fs.Position
    $magic = $br.ReadUInt16()
    if ($magic -eq 0x20B) { $ddOff = $optOff + 112 }
    elseif ($magic -eq 0x10B) { $ddOff = $optOff + 96 }
    else { throw ('unsupported optional header magic 0x' + $magic.ToString('X')) }
    $fs.Position = $ddOff
    $expRva = $br.ReadUInt32()
    $null = $br.ReadUInt32()
    if ($expRva -eq 0) { return @() }
    $secOff = $optOff + $optSize
    $sections = @()
    for ($i = 0; $i -lt $numSections; $i++) {
      $fs.Position = $secOff + ($i * 40)
      $null = $br.ReadBytes(8)
      $vsize = $br.ReadUInt32(); $vaddr = $br.ReadUInt32()
      $rsize = $br.ReadUInt32(); $raddr = $br.ReadUInt32()
      $sections += [pscustomobject]@{ VAddr = $vaddr; VSize = $vsize; RAddr = $raddr; RSize = $rsize }
    }
    $expOff = Convert-RvaToOffset $sections $expRva
    if ($expOff -lt 0) { throw 'export directory RVA outside every section' }
    $fs.Position = $expOff
    $null = $br.ReadUInt32(); $null = $br.ReadUInt32()
    $null = $br.ReadUInt16(); $null = $br.ReadUInt16()
    $null = $br.ReadUInt32(); $null = $br.ReadUInt32()
    $null = $br.ReadUInt32()
    $numNames = $br.ReadUInt32()
    $null = $br.ReadUInt32()
    $addrNames = $br.ReadUInt32()
    $null = $br.ReadUInt32()
    if ($numNames -eq 0) { return @() }
    $nameOff = Convert-RvaToOffset $sections $addrNames
    if ($nameOff -lt 0) { throw 'export name table RVA outside every section' }
    $fs.Position = $nameOff
    $nameRvas = @()
    for ($i = 0; $i -lt $numNames; $i++) { $nameRvas += $br.ReadUInt32() }
    $names = @()
    foreach ($nr in $nameRvas) {
      $off = Convert-RvaToOffset $sections $nr
      if ($off -lt 0) { continue }
      $fs.Position = $off
      $bytes = New-Object System.Collections.Generic.List[byte]
      while ($true) {
        $b = $br.ReadByte()
        if ($b -eq 0) { break }
        $bytes.Add($b)
        if ($bytes.Count -gt 512) { break }
      }
      $names += [System.Text.Encoding]::ASCII.GetString($bytes.ToArray())
    }
    return $names
  } finally {
    $br.Dispose()
    $fs.Dispose()
  }
}

# ---------------------------------------------------------------------------
# step helpers
# ---------------------------------------------------------------------------
function Get-ExpectedVersion {
  if ($Version) { return $Version.Trim() }
  $vf = Join-Path $script:Root 'VERSION'
  if (Test-Path -LiteralPath $vf) {
    $v = (Get-Content -LiteralPath $vf -TotalCount 1)
    if ($v) { return $v.Trim() }
  }
  return '0.3.0'
}

function Get-PathEntries {
  $value = (Get-ItemProperty -Path $script:EnvKey -Name 'Path' -ErrorAction SilentlyContinue).Path
  if (-not $value) { return @() }
  return @($value -split ';' | Where-Object { $_ -ne '' })
}

function Test-PathContains {
  param([string]$Needle)
  $norm = $Needle.TrimEnd('\').ToLowerInvariant()
  foreach ($e in (Get-PathEntries)) {
    if ($e.TrimEnd('\').ToLowerInvariant() -eq $norm) { return $true }
  }
  return $false
}

function Wait-ForCondition {
  param([scriptblock]$Condition, [int]$TimeoutSecondsLocal = 60, [int]$IntervalMs = 500)
  $deadline = (Get-Date).AddSeconds($TimeoutSecondsLocal)
  while ((Get-Date) -lt $deadline) {
    if (& $Condition) { return $true }
    Start-Sleep -Milliseconds $IntervalMs
  }
  if (& $Condition) { return $true }
  return $false
}

function Remove-PreviousInstall {
  if (Test-Path -LiteralPath $script:UninstallExe) {
    Write-Log 'previous installation found; removing it before the run'
    $null = Invoke-SetupExe -Exe $script:UninstallExe -Arguments '/S' -Timeout 120
    $null = Wait-ForCondition -Condition { -not (Test-Path -LiteralPath $script:InstallDir) } -TimeoutSecondsLocal 60
  }
}

# ---------------------------------------------------------------------------
# run
# ---------------------------------------------------------------------------
Write-Log ('root        : ' + $script:Root)
Write-Log ('log         : ' + $script:LogPath)
Write-Log ('powershell  : ' + $PSVersionTable.PSVersion.ToString())

$admin = ([Security.Principal.WindowsPrincipal]::new(
  [Security.Principal.WindowsIdentity]::GetCurrent()
)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
Write-Log ('elevated    : ' + $admin)
if (-not $admin) {
  Write-Log 'acceptance must run elevated (installer writes HKLM and the machine PATH)' 'FAIL'
  exit 3
}

$expectedVersion = Get-ExpectedVersion
Write-Log ('expected    : version ' + $expectedVersion)

if (-not $SetupPath) { $SetupPath = Join-Path $script:Root ('dist\inv-setup-' + $expectedVersion + '.exe') }
Write-Log ('setup       : ' + $SetupPath)

Remove-PreviousInstall

# --- step 1: installer exists and signature is readable --------------------
$step1ok = $false
if (-not (Test-Path -LiteralPath $SetupPath)) {
  Add-Result 1 'installer present' 'FAIL' ('missing file ' + $SetupPath)
} else {
  $fi = Get-Item -LiteralPath $SetupPath
  $sig = $null
  try { $sig = Get-AuthenticodeSignature -LiteralPath $SetupPath } catch { $sig = $null }
  $sigText = 'unknown'
  if ($null -ne $sig) { $sigText = [string]$sig.Status }
  $detail = 'size=' + $fi.Length + ' bytes sha256=' + (Get-FileHash -LiteralPath $SetupPath -Algorithm SHA256).Hash + ' signature=' + $sigText
  if ($null -ne $sig -and $sig.Status -eq 'Valid') {
    Add-Result 1 'installer present + signed' 'PASS' $detail
    $step1ok = $true
  } else {
    Add-Result 1 'installer present, signature not valid' 'WARN' $detail
    $step1ok = $true
    $warnings++
  }
}

# --- step 2: silent install ------------------------------------------------
$step2ok = $false
$OrigLang = ''
if (-not $step1ok) {
  Add-Result 2 'silent install' 'SKIP' 'installer unavailable'
} else {
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  $rc = Invoke-SetupExe -Exe $SetupPath -Arguments '/S' -Timeout 300
  $sw.Stop()
  if ($rc -eq 0 -and (Test-Path -LiteralPath $script:InvExe)) {
    Add-Result 2 'silent install' 'PASS' ('exit=' + $rc + ' elapsed=' + [int]$sw.Elapsed.TotalSeconds + 's')
    $step2ok = $true
    # pin the catalog to English so the later message assertions are deterministic,
    # then restore the user's language at the end of the run
    $langNow = Invoke-Inv -Arguments 'set lang'
    $lm = [regex]::Match($langNow.StdOut, 'lang\s*=\s*(\S+)')
    if ($lm.Success) { $OrigLang = $lm.Groups[1].Value }
    $null = Invoke-Inv -Arguments 'set lang en'
    Write-Log ('language pinned to en (previous: ' + $(if ($OrigLang) { $OrigLang } else { 'unknown' }) + ')')
  } else {
    Add-Result 2 'silent install' 'FAIL' ('exit=' + $rc + ' inv.exe present=' + (Test-Path -LiteralPath $script:InvExe))
  }
}

# --- step 3: version ------------------------------------------------------
$step3ok = $false
if (-not $step2ok) {
  Add-Result 3 'inv version' 'SKIP' 'not installed'
} else {
  # the kernel prints "inv <version> (vanadium <ver>, abi <n>)" - there is no JSON form.
  $r = Invoke-Inv -Arguments 'version'
  Show-CliOutput 'inv version' $r
  $reported = $null
  $m = [regex]::Match($r.StdOut, 'inv\s+([0-9]+\.[0-9]+\.[0-9]+)')
  if ($m.Success) { $reported = $m.Groups[1].Value }
  if ($r.ExitCode -eq 0 -and $reported -eq $expectedVersion) {
    Add-Result 3 'inv version' 'PASS' ('reported ' + $reported + ' :: ' + $r.StdOut.Trim())
    $step3ok = $true
  } else {
    Add-Result 3 'inv version' 'FAIL' ('expected ' + $expectedVersion + ' got [' + $reported + '] exit=' + $r.ExitCode + ' out=[' + $r.StdOut.Trim() + ']')
  }
}

# --- step 4: vanadium.dll is a loadable PE with exports --------------------
$step4ok = $false
$dll = Join-Path $script:InstallDir 'vanadium.dll'
if (-not $step2ok) {
  Add-Result 4 'vanadium.dll' 'SKIP' 'not installed'
} elseif (-not (Test-Path -LiteralPath $dll)) {
  Add-Result 4 'vanadium.dll' 'FAIL' ('missing ' + $dll)
} else {
  $exports = @()
  $peErr = ''
  try { $exports = @(Get-PeExportNames -Path $dll) } catch { $peErr = $_.Exception.Message }
  $kernelOk = $false
  $r = Invoke-Inv -Arguments 'version'
  if ($r.ExitCode -eq 0 -and $r.StdOut -match 'vanadium\s+\S+') { $kernelOk = $true }
  if ($exports.Count -gt 0 -and $kernelOk) {
    Add-Result 4 'vanadium.dll' 'PASS' ('exports=' + $exports.Count + ' [' + (($exports | Select-Object -First 6) -join ', ') + '] inv version loads the kernel')
    $step4ok = $true
  } elseif ($exports.Count -gt 0) {
    Add-Result 4 'vanadium.dll' 'WARN' ('exports=' + $exports.Count + ' but inv version failed to load the kernel: exit=' + $r.ExitCode)
    $step4ok = $true
    $warnings++
  } else {
    Add-Result 4 'vanadium.dll' 'FAIL' ('no PE exports read (' + $peErr + ')')
  }
}

# --- step 5: help lists every command -------------------------------------
$step5ok = $false
if (-not $step2ok) {
  Add-Result 5 'inv help -all' 'SKIP' 'not installed'
} else {
  # "help" with no argument prints the command table (there is no -all switch).
  $r = Invoke-Inv -Arguments 'help'
  Show-CliOutput 'inv help' $r
  $missing = @()
  foreach ($c in $script:Commands) {
    if ($r.StdOut -notmatch ('(?m)^\s*' + [regex]::Escape($c) + '\s')) { $missing += $c }
  }
  if ($r.ExitCode -eq 0 -and $missing.Count -eq 0) {
    Add-Result 5 'inv help lists every command' 'PASS' ('all ' + $script:Commands.Count + ' commands listed')
    $step5ok = $true
  } else {
    Add-Result 5 'inv help lists every command' 'FAIL' ('missing=[' + ($missing -join ',') + '] exit=' + $r.ExitCode)
  }
}

# --- step 6: set/get round trip -------------------------------------------
$step6ok = $false
if (-not $step2ok) {
  Add-Result 6 'set/get roundtrip' 'SKIP' 'not installed'
} else {
  # only declared setting keys are accepted and there is no -unset: use max_branch
  # (int, 1..1000000) and restore the original value afterwards.
  $key = 'max_branch'
  $orig = Invoke-Inv -Arguments ('set ' + $key)
  Show-CliOutput ('inv set ' + $key) $orig
  $origValue = $null
  $om = [regex]::Match($orig.StdOut, [regex]::Escape($key) + '\s*=\s*(\S+)')
  if ($om.Success) { $origValue = $om.Groups[1].Value }
  $probeValue = '512'
  $w = Invoke-Inv -Arguments ('set ' + $key + ' ' + $probeValue)
  Show-CliOutput ('inv set ' + $key + ' ' + $probeValue) $w
  $g = Invoke-Inv -Arguments ('set ' + $key)
  Show-CliOutput ('inv set ' + $key) $g
  $readBack = $g.StdOut.Trim()
  $restored = $false
  if ($origValue) {
    $null = Invoke-Inv -Arguments ('set ' + $key + ' ' + $origValue)
    $rg = Invoke-Inv -Arguments ('set ' + $key)
    $restored = ($rg.StdOut -match ('=\s*' + [regex]::Escape($origValue) + '\s*$'))
  }
  # the message catalog must be switchable at runtime (task requirement): zh-cn -> en
  # round trip. Only the stored value is asserted, so this holds even when the text
  # itself is translated.
  $null = Invoke-Inv -Arguments 'set lang zh-cn'
  $lz = Invoke-Inv -Arguments 'set lang'
  $null = Invoke-Inv -Arguments 'set lang en'
  $le = Invoke-Inv -Arguments 'set lang'
  Show-CliOutput 'inv set lang (zh-cn)' $lz
  Show-CliOutput 'inv set lang (en)' $le
  $langZh = ($lz.StdOut -match '=\s*zh-cn\s*$')
  $langEn = ($le.StdOut -match '=\s*en\s*$')
  if ($w.ExitCode -eq 0 -and $g.ExitCode -eq 0 -and $readBack -match ('=\s*' + [regex]::Escape($probeValue) + '\s*$') -and $restored -and $langZh -and $langEn) {
    Add-Result 6 'set/get roundtrip' 'PASS' ($key + '=' + $probeValue + ' round-tripped, original [' + $origValue + '] restored, lang zh-cn/en switchable')
    $step6ok = $true
  } else {
    Add-Result 6 'set/get roundtrip' 'FAIL' ('write exit=' + $w.ExitCode + ' read exit=' + $g.ExitCode + ' read=[' + $readBack + '] restored=' + $restored + ' orig=[' + $origValue + '] langZh=' + $langZh + ' langEn=' + $langEn)
  }
}

# --- step 7: temp repo init/reg/push + tree listing -----------------------
$step7ok = $false
$tempRoot = ''
$repo = ''
if (-not $step2ok) {
  Add-Result 7 'repo init/reg/push + list -tree' 'SKIP' 'not installed'
} else {
  $tempRoot = Join-Path $env:TEMP ('inv-accept-' + $stamp)
  $repo = Join-Path $tempRoot 'repo'
  New-Item -ItemType Directory -Path (Join-Path $repo 'sub') -Force | Out-Null
  Set-Content -LiteralPath (Join-Path $repo 'alpha.txt') -Value 'alpha content' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $repo 'sub\beta.txt') -Value 'beta content' -Encoding UTF8
  # NOTE: "push" uploads an .ivpack to a GitHub release (network + token) and is NOT a
  # local snapshot, so this step stays offline: init -> reg (commit) -> reg subdir -> list.
  $i = Invoke-Inv -Arguments ('init "' + $repo + '"')
  Show-CliOutput 'inv init' $i
  $g = Invoke-Inv -Arguments ('reg "' + $repo + '"') -WorkingDirectory $repo
  Show-CliOutput 'inv reg <repo>' $g
  $s = Invoke-Inv -Arguments ('reg "' + (Join-Path $repo 'sub') + '"') -WorkingDirectory $repo
  Show-CliOutput 'inv reg <repo>\sub' $s
  # kernel contract (rewritten list): plain "list" is a flat branch table
  #   /  @1  <12 hex>  [system]
  #   /sub  @1  <12 hex>
  # "list -tree" keeps the branch tree, "list <branch>" lists that branch's files.
  $l = Invoke-Inv -Arguments 'list' -WorkingDirectory $repo
  Show-CliOutput 'inv list' $l
  $t = Invoke-Inv -Arguments 'list -tree' -WorkingDirectory $repo
  Show-CliOutput 'inv list -tree' $t
  $f = Invoke-Inv -Arguments 'list /sub' -WorkingDirectory $repo
  Show-CliOutput 'inv list /sub' $f
  # scan_working_tree must not pick up the archive's own branches/ metadata: a fresh
  # commit has to leave the working tree clean.
  $d = Invoke-Inv -Arguments 'diff' -WorkingDirectory $repo
  Show-CliOutput 'inv diff' $d
  $flatRoot = ($l.StdOut -match '(?m)^/\s+@1\s+[0-9a-f]{12}\s+\[system\]')
  $flatSub = ($l.StdOut -match '(?m)^/sub\s+@1\s+[0-9a-f]{12}')
  $treeOk = ($t.StdOut -match 'branches/' -and $t.StdOut -match '\|\-\-\s+sub')
  $filesOk = ($f.StdOut -match 'beta\.txt')
  $diffClean = ($d.StdOut -match 'Nothing to do')
  if ($i.ExitCode -eq 0 -and $g.ExitCode -eq 0 -and $s.ExitCode -eq 0 -and $l.ExitCode -eq 0 -and $t.ExitCode -eq 0 -and $f.ExitCode -eq 0 -and $d.ExitCode -eq 0 -and $flatRoot -and $flatSub -and $treeOk -and $filesOk -and $diffClean) {
    Add-Result 7 'repo init/reg + list flat/tree + diff clean' 'PASS' ('repo ' + $repo)
    $step7ok = $true
  } else {
    Add-Result 7 'repo init/reg + list flat/tree + diff clean' 'FAIL' ('init=' + $i.ExitCode + ' reg=' + $g.ExitCode + ' regsub=' + $s.ExitCode + ' list=' + $l.ExitCode + ' tree=' + $t.ExitCode + ' files=' + $f.ExitCode + ' diff=' + $d.ExitCode + ' flatRoot=' + $flatRoot + ' flatSub=' + $flatSub + ' treeOk=' + $treeOk + ' filesOk=' + $filesOk + ' diffClean=' + $diffClean + ' listOut=[' + $l.StdOut.Trim() + '] treeOut=[' + $t.StdOut.Trim() + '] fileOut=[' + $f.StdOut.Trim() + '] diffOut=[' + $d.StdOut.Trim() + ']')
  }
}

# --- step 8: rollback restores the original file hash ---------------------
$step8ok = $false
if (-not $step7ok) {
  Add-Result 8 'rb rollback hash' 'SKIP' 'repository unavailable'
} else {
  $alpha = Join-Path $repo 'alpha.txt'
  $sourceHash = (Get-FileHash -LiteralPath $alpha -Algorithm SHA256).Hash
  Set-Content -LiteralPath $alpha -Value 'mutated after snapshot' -Encoding UTF8
  # "log" prints "@<seq>  <sha>  <time>  <message>"; "rb" takes <branch> <seq>.
  $logR = Invoke-Inv -Arguments 'log' -WorkingDirectory $repo
  Show-CliOutput 'inv log' $logR
  $seq = $null
  $m = [regex]::Match($logR.StdOut, '@(\d+)')
  if ($m.Success) { $seq = $m.Groups[1].Value }
  if (-not $seq) {
    Add-Result 8 'rb rollback hash' 'FAIL' 'could not determine a snapshot sequence from inv log'
  } else {
    $rb = Invoke-Inv -Arguments ('rb / ' + $seq + ' -yes') -WorkingDirectory $repo
    Show-CliOutput ('inv rb / ' + $seq) $rb
    $afterHash = $null
    if (Test-Path -LiteralPath $alpha) { $afterHash = (Get-FileHash -LiteralPath $alpha -Algorithm SHA256).Hash }
    if ($rb.ExitCode -eq 0 -and $afterHash -eq $sourceHash) {
      Add-Result 8 'rb rollback hash' 'PASS' ('snapshot @' + $seq + ' restored sha256 ' + $sourceHash)
      $step8ok = $true
    } else {
      Add-Result 8 'rb rollback hash' 'FAIL' ('rb exit=' + $rb.ExitCode + ' expected ' + $sourceHash + ' got [' + $afterHash + ']')
    }
  }
}

# --- step 9: verify -------------------------------------------------------
$step9ok = $false
if (-not $step7ok) {
  Add-Result 9 'inv verify' 'SKIP' 'repository unavailable'
} else {
  $v = Invoke-Inv -Arguments 'verify' -WorkingDirectory $repo
  Show-CliOutput 'inv verify' $v
  # cmd_verify prints "<summary>: <n> objects, <m> bad" and exits 0 only when m == 0.
  # The catalog is pinned to English above; accept either summary word but require the
  # object/bad counters so a silent failure cannot pass as a PASS.
  $verifyCounts = [regex]::Match($v.StdOut, '(\d+)\s+objects?,\s*(\d+)\s+bad')
  $verifyOk = ($v.ExitCode -eq 0 -and $verifyCounts.Success -and [int]$verifyCounts.Groups[2].Value -eq 0)
  if ($verifyOk) {
    Add-Result 9 'inv verify' 'PASS' $v.StdOut.Trim()
    $step9ok = $true
  } else {
    Add-Result 9 'inv verify' 'FAIL' ('exit=' + $v.ExitCode + ' counts=' + $verifyCounts.Success + ' out=[' + $v.StdOut.Trim() + ']')
  }
}

# --- step 10: silent uninstall cleans up ----------------------------------
if (-not $step2ok) {
  Add-Result 10 'silent uninstall cleanup' 'SKIP' 'nothing installed'
} else {
  $pathWasAdded = Test-PathContains $script:InstallDir
  $rc = Invoke-SetupExe -Exe $script:UninstallExe -Arguments '/S' -Timeout 300
  $gone = Wait-ForCondition -Condition { -not (Test-Path -LiteralPath $script:InstallDir) } -TimeoutSecondsLocal 90
  $regGone = (-not (Test-Path -LiteralPath $script:UninstallKey)) -and (-not (Test-Path -LiteralPath $script:AppPathKey))
  $pathGone = -not (Test-PathContains $script:InstallDir)
  $leftovers = @()
  if (Test-Path -LiteralPath $script:InstallDir) {
    $leftovers += @(Get-ChildItem -LiteralPath $script:InstallDir -Recurse -Force | ForEach-Object { $_.FullName })
  }
  if ($rc -eq 0 -and $gone -and $regGone -and $pathGone) {
    Add-Result 10 'silent uninstall cleanup' 'PASS' ('exit=' + $rc + ' dir removed, registry removed, PATH entry removed (entry present before: ' + $pathWasAdded + ')')
  } else {
    Add-Result 10 'silent uninstall cleanup' 'FAIL' ('exit=' + $rc + ' dirGone=' + $gone + ' regGone=' + $regGone + ' pathGone=' + $pathGone + ' leftovers=[' + ($leftovers -join ';') + ']')
  }
}

# --- restore the user's message language ----------------------------------
if ($step2ok -and $OrigLang -and $OrigLang -ne 'en') {
  $null = Invoke-Inv -Arguments ('set lang ' + $OrigLang)
  Write-Log ('language restored to ' + $OrigLang)
}

# --- cleanup of the temp repository ---------------------------------------
if ($tempRoot -and (Test-Path -LiteralPath $tempRoot)) {
  if ($KeepTemp) {
    Write-Log ('temp repository kept: ' + $tempRoot)
  } else {
    try { Remove-Item -LiteralPath $tempRoot -Recurse -Force } catch { Write-Log ('temp cleanup failed: ' + $_.Exception.Message) 'WARN' }
  }
}

# ---------------------------------------------------------------------------
# summary
# ---------------------------------------------------------------------------
Write-Log '-------------------------------------------------------------'
foreach ($res in $script:Results) {
  $line = 'step {0,2} {1,-5} {2}' -f $res.Step, $res.Status, $res.Name
  if ($res.Detail) { $line = $line + ' :: ' + $res.Detail }
  Write-Log $line
}
$failures = @($script:Results | Where-Object { $_.Status -eq 'FAIL' }).Count
$skips = @($script:Results | Where-Object { $_.Status -eq 'SKIP' }).Count
$passes = @($script:Results | Where-Object { $_.Status -eq 'PASS' }).Count
$warns = @($script:Results | Where-Object { $_.Status -eq 'WARN' }).Count
Write-Log ('summary: pass=' + $passes + ' warn=' + $warns + ' fail=' + $failures + ' skip=' + $skips)
Write-Log ('log file: ' + $script:LogPath)
if ($failures -gt 0) {
  Write-Log 'ACCEPTANCE_RESULT=FAIL'
  exit 1
}
Write-Log 'ACCEPTANCE_RESULT=PASS'
exit 0
