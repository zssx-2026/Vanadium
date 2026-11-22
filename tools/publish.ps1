<#
.SYNOPSIS
  Publish the inv installer assets to a fixed GitHub release.

.DESCRIPTION
  Uploads the silent installer, the win64 zip and SHA256SUMS.txt to the
  release identified by -Tag in zssx-2026/Infinity-Vanade, then reads the
  asset list back and compares name, size and digest with the local files.
  The token is read from the user environment variable
  gittoken_zssx-2026_1 and is never printed or written to disk.

.NOTES
  0.5.0 native-transport gate: run these before publishing. The guardrails are
  advisory by default (they print ADVISORY lines and keep a 0.4.x run green),
  so the switches below are what turn them into hard failures.

    cmake --build build\cmake --parallel 1
    powershell -NoProfile -ExecutionPolicy Bypass -File tests\installer-static.ps1 -RequireNativeTransport
      expect: checks=14 failures=0 / STATIC_RESULT=PASS
              (no WinHTTP in src/include/CMakeLists.txt, WINHTTP.dll gone from the
               import table, WS2_32 + CRYPT32 + SECUR32 present)
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\acceptance.ps1 -RequireNativeTransport
      expect: step 4b PASS ("transport  native (winsock2 + schannel)") and
              step 8b PASS (downloader local range server: rc=0 + sha256) and
              ACCEPTANCE_RESULT=PASS with fail=0
              (step 4c asserts the "abi vanadium-abi-1" tag unconditionally)

.PARAMETER Tag
  Release tag. Defaults to inv. Snapshot/alpha builds pass e.g.
  -Tag inv-0.5.0-snapshot.2 -Version 0.5.0-snapshot.2 -Prerelease.

.PARAMETER Repo
  owner/name. Defaults to zssx-2026/Infinity-Vanade.

.PARAMETER Version
  Product version used to locate dist\inv-setup-<version>.exe. Defaults to
  the first line of <repo>\VERSION, then to 0.1.0.

.PARAMETER DistDir
  Directory holding the artifacts. Defaults to <repo>\dist.

.PARAMETER Title
  Release title. Defaults to "inv <version>".

.PARAMETER DryRun
  Print the plan and local hashes without any network call.

.PARAMETER KeepExisting
  Leave assets that already exist on the release untouched instead of
  deleting and re-uploading them.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish.ps1 -DryRun
#>
[CmdletBinding()]
param(
  [string]$Tag = 'inv',
  [string]$Repo = 'zssx-2026/Infinity-Vanade',
  [string]$Version = '',
  [string]$DistDir = '',
  [string]$Notes = '',
  [string]$Title = '',
  # snapshot/alpha builds must NOT land on the stable 'inv' release; they get
  # their own tag and are flagged as pre-release so GitHub sorts them apart.
  [switch]$Prerelease,
  [switch]$DryRun,
  [switch]$KeepExisting
)

$ErrorActionPreference = 'Stop'
$script:Root = Split-Path -Parent $PSScriptRoot

function Write-Log {
  param([string]$Text)
  Write-Host $Text
}

function Get-JsonProp {
  param($Object, [string]$Name)
  if ($null -eq $Object) { return $null }
  foreach ($p in $Object.PSObject.Properties) {
    if ($p.Name -ieq $Name) { return $p.Value }
  }
  return $null
}

function Get-Sha256 {
  param([string]$Path)
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
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

# ---------------------------------------------------------------------------
# local artifacts
# ---------------------------------------------------------------------------
$expectedVersion = Get-ExpectedVersion
if (-not $DistDir) { $DistDir = Join-Path $script:Root 'dist' }
$setup = Join-Path $DistDir ('inv-setup-' + $expectedVersion + '.exe')
$zip = Join-Path $DistDir 'inv-win64.zip'
$sums = Join-Path $DistDir 'SHA256SUMS.txt'

$missing = @()
foreach ($f in @($setup, $zip)) {
  if (-not (Test-Path -LiteralPath $f)) { $missing += $f }
}
if ($missing.Count -gt 0) {
  Write-Log ('PUBLISH_RESULT=FAIL missing local artifact(s): ' + ($missing -join '; '))
  exit 2
}

$local = @()
foreach ($f in @($setup, $zip)) {
  $local += [pscustomobject]@{
    Name = (Split-Path $f -Leaf)
    Path = $f
    Size = (Get-Item -LiteralPath $f).Length
    Sha256 = (Get-Sha256 $f)
  }
}

$sumLines = @()
foreach ($item in $local) {
  $sumLines += ($item.Sha256 + '  ' + $item.Name)
}
$sumText = ($sumLines -join [Environment]::NewLine) + [Environment]::NewLine
Set-Content -LiteralPath $sums -Value $sumText -Encoding ASCII -NoNewline
$local += [pscustomobject]@{
  Name = 'SHA256SUMS.txt'
  Path = $sums
  Size = (Get-Item -LiteralPath $sums).Length
  Sha256 = (Get-Sha256 $sums)
}

Write-Log ('publish target : ' + $Repo + ' tag=' + $Tag)
Write-Log ('dist directory : ' + $DistDir)
foreach ($item in $local) {
  Write-Log ('  local ' + $item.Name + ' size=' + $item.Size + ' sha256=' + $item.Sha256)
}

if ($DryRun) {
  Write-Log 'dry run: no network call was made'
  Write-Log 'PUBLISH_RESULT=DRYRUN'
  exit 0
}

# ---------------------------------------------------------------------------
# token (never printed)
# ---------------------------------------------------------------------------
$token = [Environment]::GetEnvironmentVariable('gittoken_zssx-2026_1', 'User')
if (-not $token) {
  Write-Log 'PUBLISH_RESULT=FAIL token environment variable gittoken_zssx-2026_1 is empty'
  exit 3
}

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.Net.Http | Out-Null

$client = New-Object System.Net.Http.HttpClient
$client.Timeout = [TimeSpan]::FromMinutes(30)
$client.DefaultRequestHeaders.UserAgent.ParseAdd('inv-publisher/1.0')
$client.DefaultRequestHeaders.Authorization = New-Object System.Net.Http.Headers.AuthenticationHeaderValue('Bearer', $token)
$client.DefaultRequestHeaders.Accept.ParseAdd('application/vnd.github+json')

$apiBase = 'https://api.github.com/repos/' + $Repo
$uploadBase = 'https://uploads.github.com/repos/' + $Repo

function Read-Response {
  param([System.Net.Http.HttpResponseMessage]$Response)
  $text = $Response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
  $json = $null
  if ($text) { try { $json = ConvertFrom-Json -InputObject $text } catch { $json = $null } }
  return [pscustomobject]@{ Status = [int]$Response.StatusCode; Json = $json; Text = $text }
}

function Get-Release {
  $req = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, ($apiBase + '/releases/tags/' + $Tag))
  $resp = $client.SendAsync($req).GetAwaiter().GetResult()
  return (Read-Response $resp)
}

function New-Release {
  $releaseName = 'inv ' + $expectedVersion
  if ($Title) { $releaseName = $Title }
  $payload = @{
    tag_name = $Tag
    name = $releaseName
    body = $Notes
    draft = $false
    prerelease = [bool]$Prerelease
  }
  $json = $payload | ConvertTo-Json -Depth 5
  $content = New-Object System.Net.Http.StringContent($json, [System.Text.Encoding]::UTF8, 'application/json')
  $req = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, ($apiBase + '/releases'))
  $req.Content = $content
  $resp = $client.SendAsync($req).GetAwaiter().GetResult()
  return (Read-Response $resp)
}

function Get-Assets {
  param([int]$ReleaseId)
  $req = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, ($apiBase + '/releases/' + $ReleaseId + '/assets?per_page=100'))
  $resp = $client.SendAsync($req).GetAwaiter().GetResult()
  return (Read-Response $resp)
}

function Remove-Asset {
  param([int]$AssetId)
  $req = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Delete, ($apiBase + '/releases/assets/' + $AssetId))
  $resp = $client.SendAsync($req).GetAwaiter().GetResult()
  return [int]$resp.StatusCode
}

function Send-Asset {
  param([int]$ReleaseId, [string]$Path, [string]$Name)
  $url = $uploadBase + '/releases/' + $ReleaseId + '/assets?name=' + [uri]::EscapeDataString($Name)
  $fs = [System.IO.File]::OpenRead($Path)
  try {
    $content = New-Object System.Net.Http.StreamContent($fs)
    $content.Headers.ContentType = New-Object System.Net.Http.Headers.MediaTypeHeaderValue('application/octet-stream')
    $req = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, $url)
    $req.Content = $content
    $resp = $client.SendAsync($req).GetAwaiter().GetResult()
    return (Read-Response $resp)
  } finally {
    $fs.Dispose()
  }
}

# ---------------------------------------------------------------------------
# release lookup / creation
# ---------------------------------------------------------------------------
$release = $null
$rel = Get-Release
if ($rel.Status -eq 200) {
  $release = $rel.Json
  Write-Log ('release found  : id=' + (Get-JsonProp $release 'id') + ' tag=' + $Tag)
} elseif ($rel.Status -eq 404) {
  Write-Log 'release not found; creating it'
  $created = New-Release
  if ($created.Status -ne 201) {
    Write-Log ('PUBLISH_RESULT=FAIL could not create release: status=' + $created.Status + ' body=' + $created.Text)
    exit 4
  }
  $release = $created.Json
  Write-Log ('release created: id=' + (Get-JsonProp $release 'id') + ' tag=' + $Tag)
} else {
  Write-Log ('PUBLISH_RESULT=FAIL release lookup failed: status=' + $rel.Status + ' body=' + $rel.Text)
  exit 4
}

$releaseId = [int](Get-JsonProp $release 'id')
$existing = Get-Assets -ReleaseId $releaseId
if ($existing.Status -ne 200) {
  Write-Log ('PUBLISH_RESULT=FAIL asset listing failed: status=' + $existing.Status)
  exit 5
}
$existingAssets = @($existing.Json)
Write-Log ('existing assets: ' + $existingAssets.Count)

# ---------------------------------------------------------------------------
# upload
# ---------------------------------------------------------------------------
$uploaded = @()
foreach ($item in $local) {
  $same = @($existingAssets | Where-Object { (Get-JsonProp $_ 'name') -eq $item.Name })
  foreach ($old in $same) {
    if ($KeepExisting) {
      Write-Log ('keep existing  : ' + $item.Name)
      continue
    }
    $oldId = [int](Get-JsonProp $old 'id')
    $delStatus = Remove-Asset -AssetId $oldId
    Write-Log ('removed old    : ' + $item.Name + ' id=' + $oldId + ' status=' + $delStatus)
  }
  if ($KeepExisting -and $same.Count -gt 0) { continue }
  Write-Log ('uploading      : ' + $item.Name + ' (' + $item.Size + ' bytes)')
  $up = Send-Asset -ReleaseId $releaseId -Path $item.Path -Name $item.Name
  if ($up.Status -ne 201) {
    Write-Log ('PUBLISH_RESULT=FAIL upload failed for ' + $item.Name + ': status=' + $up.Status + ' body=' + $up.Text)
    exit 6
  }
  $uploaded += $item.Name
  Write-Log ('uploaded       : ' + $item.Name)
}

# ---------------------------------------------------------------------------
# read back and compare
# ---------------------------------------------------------------------------
$after = Get-Assets -ReleaseId $releaseId
if ($after.Status -ne 200) {
  Write-Log ('PUBLISH_RESULT=FAIL readback listing failed: status=' + $after.Status)
  exit 7
}
$afterAssets = @($after.Json)
$failures = 0
foreach ($item in $local) {
  $remote = @($afterAssets | Where-Object { (Get-JsonProp $_ 'name') -eq $item.Name }) | Select-Object -First 1
  if (-not $remote) {
    Write-Log ('READBACK FAIL  : ' + $item.Name + ' missing on the release')
    $failures++
    continue
  }
  $remoteSize = [int64](Get-JsonProp $remote 'size')
  $remoteDigest = [string](Get-JsonProp $remote 'digest')
  $digestOk = $false
  if ($remoteDigest) {
    $digestOk = ($remoteDigest.ToLowerInvariant() -eq ('sha256:' + $item.Sha256))
  } else {
    $digestOk = ($remoteSize -eq $item.Size)
  }
  if ($remoteSize -eq $item.Size -and $digestOk) {
    Write-Log ('READBACK PASS  : ' + $item.Name + ' size=' + $remoteSize + ' digest=' + $remoteDigest)
  } else {
    Write-Log ('READBACK FAIL  : ' + $item.Name + ' local size=' + $item.Size + ' remote size=' + $remoteSize + ' remote digest=' + $remoteDigest + ' local sha256=' + $item.Sha256)
    $failures++
  }
}

$browse = 'https://github.com/' + $Repo + '/releases/tag/' + $Tag
Write-Log ('release url    : ' + $browse)
if ($failures -gt 0) {
  Write-Log ('PUBLISH_RESULT=FAIL mismatches=' + $failures)
  exit 1
}
Write-Log 'PUBLISH_RESULT=PASS'
exit 0
