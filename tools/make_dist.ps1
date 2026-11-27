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
# Rebuild the tree payloads from scratch: Copy-Item -Recurse into an existing
# directory nests the source inside it (java\runtime\runtime), so clear the
# whole java/docs trees BEFORE any file copy (otherwise the jar copied by the
# payload loop above gets wiped by this cleanup).
foreach ($dir in @("java", "docs")) {
  $dst = Join-Path $Dist $dir
  if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
}
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

# Product icon (referenced by installer Icon/UniconIcon).
Copy-Item -Force (Join-Path $Root "shell\assets\inv.ico") (Join-Path $Dist "inv.ico")

Write-Output ("DIST_VERSION=" + $Version)

# Completeness gate: the payload must contain the jar AND the runtime, or the
# portable build silently loses the Java sidecar.
$required = @("inv.exe", "vanadium.dll", "VERSION", "inv.ico", "java\vanadium-report.jar", "java\runtime\bin\java.exe")
foreach ($rel in $required) {
  if (-not (Test-Path (Join-Path $Dist $rel))) { Write-Output ("MISSING dist\" + $rel); $fail++ }
}
$dll = Join-Path $Dist "vanadium.dll"
if (Test-Path $dll) {
  Write-Output ("DLL vanadium.dll " + (Get-Item $dll).Length + " " + (Get-FileHash -Algorithm SHA256 $dll).Hash.ToLower())
}

# Portable archive: everything needed to run inv from a folder (no installer).
$Zip = Join-Path $Dist "inv-win64.zip"
if (Test-Path $Zip) { Remove-Item -Force $Zip }
$Staging = Join-Path $env:TEMP ("inv-zip-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $Staging | Out-Null
foreach ($item in @("inv.exe", "vanadium.dll", "VERSION")) {
  Copy-Item -Force (Join-Path $Dist $item) (Join-Path $Staging $item)
}
foreach ($dir in @("java", "docs")) {
  Copy-Item -Recurse -Force (Join-Path $Dist $dir) (Join-Path $Staging $dir)
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($Staging, $Zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)
Remove-Item -Recurse -Force $Staging
$zipHash = (Get-FileHash -Algorithm SHA256 $Zip).Hash.ToLower()
$zipSize = (Get-Item $Zip).Length
Write-Output ("ZIP inv-win64.zip " + $zipSize + " " + $zipHash)

Get-ChildItem $Dist -Recurse -File | Where-Object { $_.Name -notlike "*.dll" -or $true } |
  Sort-Object FullName | ForEach-Object {
    $rel = $_.FullName.Substring($Dist.Length + 1)
    $h = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower()
    Write-Output ("PAYLOAD " + $rel + " " + $_.Length + " " + $h)
  }
if ($fail -gt 0) { Write-Output ("DIST_RESULT=FAIL missing=" + $fail); exit 4 }
Write-Output "DIST_RESULT=PASS"
