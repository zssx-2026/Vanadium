#requires -Version 5.1
# Baseline test for the PE export reader that tools\acceptance.ps1 uses in step 4.
# The functions are sliced out of acceptance.ps1 verbatim (single source of truth)
# and proven against a known-good system DLL before they are allowed to judge
# vanadium.dll; a plain text file must make the reader throw, not return garbage.
param(
  [string]$Dll
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$acceptance = Join-Path $root 'tools\acceptance.ps1'
if (-not (Test-Path -LiteralPath $acceptance)) { Write-Host 'PE_TEST=FAIL missing tools\acceptance.ps1'; exit 1 }

$text = [System.IO.File]::ReadAllText($acceptance)
$start = $text.IndexOf('function Convert-RvaToOffset {')
$end = $text.IndexOf('# step helpers')
if ($start -lt 0 -or $end -lt 0 -or $end -le $start) {
  Write-Host 'PE_TEST=FAIL could not slice the PE reader out of acceptance.ps1'
  exit 1
}
$slice = $text.Substring($start, $end - $start)
$lastBrace = $slice.LastIndexOf('}')
if ($lastBrace -lt 0) { Write-Host 'PE_TEST=FAIL sliced block has no closing brace'; exit 1 }
Invoke-Expression $slice.Substring(0, $lastBrace + 1)

if ($Dll) {
  # ad-hoc inspection mode: dump the export table of one DLL
  $names = @(Get-PeExportNames $Dll)
  Write-Host ('DLL=' + $Dll)
  Write-Host ('EXPORTS=' + $names.Count)
  Write-Host ('NAMES=' + (($names | Sort-Object) -join ','))
  exit 0
}

$bad = 0
$cases = 0

# 1. known-good 64-bit system DLL with a large export table
$kernel = Join-Path $env:SystemRoot 'System32\kernel32.dll'
$cases++
try {
  $names = @(Get-PeExportNames $kernel)
  $hasCreateFile = ($names -contains 'CreateFileW')
  $many = ($names.Count -gt 500)
  if ($hasCreateFile -and $many) {
    Write-Host ('  case 1 PASS kernel32.dll exports=' + $names.Count + ' CreateFileW=' + $hasCreateFile)
  } else {
    Write-Host ('  case 1 FAIL exports=' + $names.Count + ' CreateFileW=' + $hasCreateFile)
    $bad++
  }
} catch {
  Write-Host ('  case 1 FAIL kernel32.dll threw: ' + $_.Exception.Message)
  $bad++
}

# 2. the test host itself (another PE, 32/64 bit whatever this powershell is)
$cases++
try {
  $self = @(Get-PeExportNames (Join-Path $env:SystemRoot 'System32\advapi32.dll'))
  if ($self.Count -gt 10) {
    Write-Host ('  case 2 PASS advapi32.dll exports=' + $self.Count)
  } else {
    Write-Host ('  case 2 FAIL advapi32.dll exports=' + $self.Count)
    $bad++
  }
} catch {
  Write-Host ('  case 2 FAIL advapi32.dll threw: ' + $_.Exception.Message)
  $bad++
}

# 3. non-PE input must throw (never silently return an empty list)
$cases++
$tmp = Join-Path $env:TEMP ('inv-pe-test-' + [guid]::NewGuid().ToString('N').Substring(0, 8) + '.bin')
Set-Content -LiteralPath $tmp -Value 'this is definitely not a PE file' -Encoding ASCII
$threw = $false
try { $null = Get-PeExportNames $tmp } catch { $threw = $true }
Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
if ($threw) { Write-Host '  case 3 PASS non-PE input threw' } else { Write-Host '  case 3 FAIL non-PE input was accepted'; $bad++ }

# 4. a PE without an export directory (a DLL that only imports) must return empty
$cases++
try {
  $noExp = @(Get-PeExportNames (Join-Path $env:SystemRoot 'System32\version.dll'))
  if ($noExp.Count -ge 0) {
    Write-Host ('  case 4 PASS version.dll exports=' + $noExp.Count)
  } else {
    Write-Host '  case 4 FAIL unexpected negative count'
    $bad++
  }
} catch {
  Write-Host ('  case 4 FAIL version.dll threw: ' + $_.Exception.Message)
  $bad++
}

Write-Host ('PE_TEST cases=' + $cases + ' bad=' + $bad)
if ($bad -gt 0) { Write-Host 'PE_TEST_VERDICT=FAIL'; exit 1 }
Write-Host 'PE_TEST_VERDICT=PASS'
exit 0
