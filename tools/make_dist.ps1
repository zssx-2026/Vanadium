# Assemble the distributable payload in dist\ from build outputs.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_dist.ps1 [-Clean]
param([switch]$Clean)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
$Dist = Join-Path $Root "dist"
if ($Clean -and (Test-Path $Dist)) { Remove-Item $Dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Dist | Out-Null

$Version = (Get-Content (Join-Path $Root "VERSION") -Raw).Trim()
$Payload = @(
  @{ Src = "shell\target\release\inv.exe";        Dst = "inv.exe" },
  @{ Src = "build\cmake\vanadium.dll";              Dst = "vanadium.dll" },
  @{ Src = "java\out\vanadium-report.jar";         Dst = "java\vanadium-report.jar" }
)
$fail = 0
foreach ($p in $Payload) {
  $src = Join-Path $Root $p.Src
  if (-not (Test-Path $src)) { Write-Output ("MISSING " + $p.Src); $fail++; continue }
  $dst = Join-Path $Dist $p.Dst
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
  Copy-Item -Force $src $dst
}
if (Test-Path (Join-Path $Root "java\runtime")) {
  Copy-Item -Recurse -Force (Join-Path $Root "java\runtime") (Join-Path $Dist "java\runtime")
} else { Write-Output "MISSING java\runtime"; $fail++ }
if (Test-Path (Join-Path $Root "docs")) {
  Copy-Item -Recurse -Force (Join-Path $Root "docs") (Join-Path $Dist "docs")
} else { Write-Output "MISSING docs"; $fail++ }
Set-Content -Path (Join-Path $Dist "VERSION") -Value $Version -NoNewline

Write-Output ("DIST_VERSION=" + $Version)
Get-ChildItem $Dist -Recurse -File | Where-Object { $_.Name -notlike "*.dll" -or $true } |
  Sort-Object FullName | ForEach-Object {
    $rel = $_.FullName.Substring($Dist.Length + 1)
    $h = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower()
    Write-Output ("PAYLOAD " + $rel + " " + $_.Length + " " + $h)
  }
if ($fail -gt 0) { Write-Output ("DIST_RESULT=FAIL missing=" + $fail); exit 4 }
Write-Output "DIST_RESULT=PASS"
