# Generates a minimal but complete v1 archive fixture for the vanadium-report tests.
# Usage: pwsh java/tests/make-fixture.ps1 [-OutDir <dir>]
# Layout follows include/vanadium/store.h: .inv/meta.json + objects/ + branches/<id>/manifest.json
# (the reader also accepts the older archive.json name from docs/DESIGN.md).
# The output directory must be named "fixture" so a typo can never delete real data.
[CmdletBinding()]
param([string]$OutDir = '')

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $here 'fixture' }
$OutDir = [IO.Path]::GetFullPath($OutDir)
if ((Split-Path -Leaf $OutDir) -ne 'fixture') {
  throw "refusing to write outside a directory named 'fixture': $OutDir"
}
if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }

$inv = Join-Path $OutDir '.inv'
foreach ($d in @('objects', 'branches', 'tmp', 'locks')) {
  New-Item -ItemType Directory -Force -Path (Join-Path $inv $d) | Out-Null
}

$utf8 = New-Object System.Text.UTF8Encoding($false)
$script:objectBytes = @{}

function Get-Sha256Hex([byte[]]$Bytes) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try { $hash = $sha.ComputeHash($Bytes) } finally { $sha.Dispose() }
  return ([BitConverter]::ToString($hash)).Replace('-', '').ToLowerInvariant()
}

function Add-Object([byte[]]$Bytes) {
  $sha = Get-Sha256Hex $Bytes
  if (-not $script:objectBytes.ContainsKey($sha)) {
    $dir = Join-Path $inv ('objects\' + $sha.Substring(0, 2))
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    [IO.File]::WriteAllBytes((Join-Path $dir $sha), $Bytes)
    $script:objectBytes[$sha] = $Bytes.Length
  }
  return $sha
}

function New-FileEntry([string]$Path, [string]$Content) {
  $bytes = $utf8.GetBytes($Content)
  $sha = Add-Object $bytes
  return [ordered]@{
    path  = $Path
    dir   = $false
    size  = $bytes.Length
    sha   = $sha
    crc   = '0000000000000000'
    mtime = 1763200000000
    mode  = 420
  }
}

function New-DirEntry([string]$Path) {
  return [ordered]@{
    path  = $Path
    dir   = $true
    size  = 0
    sha   = ''
    crc   = '0000000000000000'
    mtime = 1763200000000
    mode  = 16877
  }
}

function Write-JsonFile([string]$Path, $Value) {
  $dir = Split-Path -Parent $Path
  if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
  $json = $Value | ConvertTo-Json -Depth 10
  [IO.File]::WriteAllText($Path, $json, $utf8)
}

function New-SnapshotId([string]$Seed) {
  return Get-Sha256Hex ($utf8.GetBytes("vanadium-fixture-snapshot:$Seed"))
}

$mainC = @'
#include <stdio.h>

int main(void) {
  printf("vanadium demo\n");
  return 0;
}
'@

$utilC = @'
unsigned long vd_crc64(const unsigned char *data, unsigned long len);
'@

$readme = @'
vanadium demo archive
'@

$design = @'
# Design

The archive keeps content-addressed objects and per-branch manifests.
'@

$devC = @'
int vd_dev_probe(void) { return 1; }
'@

$xTxt = @'
feature x payload
'@

$notesTxt = @'
feature notes: two lines
second line
'@

$releaseNotes = @'
v1.0 release notes
'@

# --- main (head_seq 2, one historical snapshot) -----------------------------
$mainEntries = @(
  (New-DirEntry 'src'),
  (New-FileEntry 'src/main.c' $mainC),
  (New-FileEntry 'src/util.c' $utilC),
  (New-FileEntry 'README.md' $readme),
  (New-DirEntry 'docs'),
  (New-FileEntry 'docs/design.md' $design),
  (New-FileEntry 'docs/copy.md' $readme)
)
$mainSnap1 = New-SnapshotId 'main#1'
$mainSnap2 = New-SnapshotId 'main#2'
Write-JsonFile (Join-Path $inv 'branches\b-main\manifest.json') ([ordered]@{
  seq        = 2
  branch     = '/branches/main'
  time_ms    = 1763200100000
  message    = 'add docs'
  parent_sha = $mainSnap1
  id         = $mainSnap2
  entries    = $mainEntries
})
Write-JsonFile (Join-Path $inv 'branches\b-main\history\1.json') ([ordered]@{
  seq        = 1
  branch     = '/branches/main'
  time_ms    = 1763200000000
  message    = 'initial import'
  parent_sha = ''
  id         = $mainSnap1
  entries    = @(
    (New-FileEntry 'src/main.c' $mainC),
    (New-FileEntry 'README.md' $readme)
  )
})

# --- main/dev ---------------------------------------------------------------
Write-JsonFile (Join-Path $inv 'branches\b-main-dev\manifest.json') ([ordered]@{
  seq        = 1
  branch     = '/branches/main/dev'
  time_ms    = 1763200200000
  message    = 'dev branch'
  parent_sha = $mainSnap2
  id         = (New-SnapshotId 'main/dev#1')
  entries    = @(
    (New-FileEntry 'README.md' $readme),
    (New-FileEntry 'src/main.c' $mainC),
    (New-FileEntry 'src/dev.c' $devC)
  )
})

# --- feature-x --------------------------------------------------------------
Write-JsonFile (Join-Path $inv 'branches\b-feature\manifest.json') ([ordered]@{
  seq        = 1
  branch     = '/branches/feature-x'
  time_ms    = 1763200300000
  message    = 'feature x'
  parent_sha = ''
  id         = (New-SnapshotId 'feature-x#1')
  entries    = @(
    (New-FileEntry 'README.md' $readme),
    (New-FileEntry 'feature/x.txt' $xTxt),
    (New-FileEntry 'feature/notes.txt' $notesTxt)
  )
})

# --- releases/tag/v1.0 (system branch) --------------------------------------
Write-JsonFile (Join-Path $inv 'branches\b-rel-tag\manifest.json') ([ordered]@{
  seq        = 1
  branch     = '/releases/tag/v1.0'
  time_ms    = 1763200400000
  message    = 'tag v1.0'
  parent_sha = ''
  id         = (New-SnapshotId 'releases/tag/v1.0#1')
  entries    = @(
    (New-FileEntry 'README.md' $readme),
    (New-FileEntry 'release-notes.md' $releaseNotes)
  )
})

# --- .inv/meta.json ---------------------------------------------------------
Write-JsonFile (Join-Path $inv 'meta.json') ([ordered]@{
  format     = 1
  id         = (Get-Sha256Hex ($utf8.GetBytes('vanadium-fixture-archive')))
  name       = 'demo-archive'
  type       = 'nodeonly'
  created_ms = 1763200000000
  branches   = @(
    [ordered]@{ id = 'b-main'; path = '/branches/main'; name = 'main'; parent = '/'; created_ms = 1763200000000; head_seq = 2; is_node = $true; children = @('/branches/main/dev'); system = $false },
    [ordered]@{ id = 'b-main-dev'; path = '/branches/main/dev'; name = 'dev'; parent = '/branches/main'; created_ms = 1763200200000; head_seq = 1; is_node = $false; children = @(); system = $false },
    [ordered]@{ id = 'b-feature'; path = '/branches/feature-x'; name = 'feature-x'; parent = '/'; created_ms = 1763200300000; head_seq = 1; is_node = $false; children = @(); system = $false },
    [ordered]@{ id = 'b-rel-tag'; path = '/releases/tag/v1.0'; name = 'v1.0'; parent = '/'; created_ms = 1763200400000; head_seq = 1; is_node = $false; children = @(); system = $true }
  )
})

Write-Output "fixture   : $OutDir"
Write-Output "objects   : $($script:objectBytes.Count)"
Write-Output "bytes     : $(($script:objectBytes.Values | Measure-Object -Sum).Sum)"
Write-Output 'FIXTURE OK'
