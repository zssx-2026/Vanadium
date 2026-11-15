# Build vanadium-report.jar and the jlink java.base runtime.
# Usage: pwsh java/build.ps1 [-JavaHome <jdk>] [-Configuration Release] [-NoRuntime] [-MinFreeMB 200] [-MaxWaitSec 120]
#
# Memory policy (this machine often has < 300 MB free while other teams run heavy services):
#   * javac heap is capped at 256 MB and compiled in ONE serial invocation (never parallel forks);
#   * jlink is capped at 256 MB and emits only java.base;
#   * a free-memory precheck prints WARN and waits (never fails silently) before building.
# Runtime flags the kernel/shell must use when spawning this jar:
#   -Xmx192m -XX:ReservedCodeCacheSize=32m -XX:TieredStopAtLevel=1 -Xshare:auto
# (the JVM flag is -Xshare:auto with a colon; "-Xshare=auto" is rejected by java 25)
# Compatible with Windows PowerShell 5.1 and PowerShell 7. Never opens a GUI dialog.
[CmdletBinding()]
param(
  [string]$Configuration = 'Release',
  [string]$JavaHome = '',
  [switch]$NoRuntime,
  [int]$MinFreeMB = 200,
  [int]$MaxWaitSec = 120
)

$ErrorActionPreference = 'Stop'
$javaDir = $PSScriptRoot
$srcDir = Join-Path $javaDir 'src\main\java'
$outDir = Join-Path $javaDir 'out'
$classesDir = Join-Path $outDir 'classes'
$jarPath = Join-Path $outDir 'vanadium-report.jar'
$runtimeDir = Join-Path $javaDir 'runtime'
$infoPath = Join-Path $outDir 'build-info.txt'

$javacHeap = '256m'
$jlinkHeap = '256m'
$runFlags = '-Xmx192m -XX:ReservedCodeCacheSize=32m -XX:TieredStopAtLevel=1 -Xshare:auto'

function Resolve-JavaHome {
  param([string]$Hint)
  $candidates = New-Object System.Collections.Generic.List[string]
  if ($Hint) { $candidates.Add($Hint) }
  # The pinned JDK from the project environment description wins over a stray JAVA_HOME.
  $candidates.Add('C:\Program Files\Java\jdk-25.0.3')
  if ($env:JAVA_HOME) { $candidates.Add($env:JAVA_HOME) }
  $javacCmd = Get-Command javac.exe -ErrorAction SilentlyContinue
  if ($javacCmd) { $candidates.Add((Split-Path -Parent (Split-Path -Parent $javacCmd.Source))) }
  foreach ($c in $candidates) {
    if ($c -and (Test-Path (Join-Path $c 'bin\javac.exe'))) { return $c }
  }
  Fail 'javac.exe not found; pass -JavaHome <jdk-dir>'
}

function Assert-UnderJavaDir {
  param([string]$Path)
  $full = [IO.Path]::GetFullPath($Path)
  $root = [IO.Path]::GetFullPath($javaDir)
  if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
    Fail "refusing to modify a path outside java/: $full"
  }
}

function Get-FreeMemMB {
  try {
    $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
    return [int]([math]::Round($os.FreePhysicalMemory / 1024))
  } catch {
    return -1
  }
}

function Wait-ForMemory {
  param([int]$NeedMB, [int]$MaxWait)
  $free = Get-FreeMemMB
  if ($free -lt 0) {
    Write-Output 'WARN      : free physical memory unknown (CIM unavailable); building serially'
    return
  }
  if ($free -ge $NeedMB) {
    Write-Output "memory    : $free MB free"
    return
  }
  Write-Output "WARN      : only $free MB free (< $NeedMB MB needed); waiting up to $MaxWait s for other builds to finish"
  $deadline = (Get-Date).AddSeconds($MaxWait)
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 10
    $free = Get-FreeMemMB
    if ($free -ge $NeedMB) {
      Write-Output "memory    : $free MB free (recovered)"
      return
    }
  }
  Write-Output "WARN      : still only $free MB free after $MaxWait s; continuing with one serial build (javac -J-Xmx$javacHeap)"
}

# Runs a native tool and returns its exit code plus merged stdout/stderr. Native stderr is
# turned into ErrorRecords by PowerShell 5.1, so the error preference is relaxed locally.
function Invoke-Native {
  param([string]$Exe, [string[]]$ArgList)
  $prev = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $captured = & $Exe @ArgList 2>&1
    $code = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $prev
  }
  $lines = @($captured | ForEach-Object { $_.ToString() })
  return [pscustomobject]@{ Code = $code; Lines = $lines; Text = ($lines -join [Environment]::NewLine) }
}

# Failures print an explicit ERROR line and stop with exit 1 (never a raw stack trace,
# never a silent success).
function Fail([string]$Message) {
  Write-Output ('ERROR     : ' + $Message)
  Write-Output 'BUILD FAILED'
  exit 1
}

$JavaHome = Resolve-JavaHome -Hint $JavaHome
$javac = Join-Path $JavaHome 'bin\javac.exe'
$jarTool = Join-Path $JavaHome 'bin\jar.exe'
$jlink = Join-Path $JavaHome 'bin\jlink.exe'
# Force English diagnostics so build logs stay ASCII and greppable.
$jvmLocale = @('-J-Duser.language=en', '-J-Duser.country=US')

Write-Output "java home : $JavaHome"
Write-Output "config    : $Configuration"
Write-Output "run flags : $runFlags"

Wait-ForMemory -NeedMB $MinFreeMB -MaxWait $MaxWaitSec

$ver = Invoke-Native $javac ($jvmLocale + @('-version'))
if ($ver.Code -ne 0) { Fail "javac -version failed (exit $($ver.Code)): $($ver.Text)" }
$javacVersion = ($ver.Text.Trim())
Write-Output "javac     : $javacVersion"

Assert-UnderJavaDir $outDir
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force -Path $classesDir | Out-Null

$sources = @(Get-ChildItem -Path $srcDir -Recurse -Filter *.java | Sort-Object FullName | Select-Object -ExpandProperty FullName)
if ($sources.Count -eq 0) { Fail "no java sources under $srcDir" }
Write-Output "sources   : $($sources.Count) file(s)"

# One javac invocation with an explicit argument list (no @argfile, which breaks under some
# Windows shells), heap capped to keep the machine alive.
$javacArgs = New-Object System.Collections.Generic.List[string]
foreach ($a in $jvmLocale) { $javacArgs.Add($a) }
foreach ($a in @("-J-Xmx$javacHeap", '--release', '21', '-encoding', 'UTF-8', '-Xlint:all', '-d', $classesDir)) { $javacArgs.Add($a) }
foreach ($s in $sources) { $javacArgs.Add($s) }
Write-Output "javac cmd : javac $(($jvmLocale -join ' ')) -J-Xmx$javacHeap --release 21 -encoding UTF-8 -Xlint:all -d out/classes <$($sources.Count) sources>"

$compiled = Invoke-Native $javac $javacArgs.ToArray()
if ($compiled.Text) { Write-Output $compiled.Text }
if ($compiled.Code -ne 0) { Fail "javac failed with exit code $($compiled.Code)" }
$classCount = @(Get-ChildItem -Path $classesDir -Recurse -Filter *.class).Count
Write-Output "classes   : $classCount file(s)"

$jarRun = Invoke-Native $jarTool (@('--create', '--file', $jarPath, '--main-class', 'inv.report.Main', '-C', $classesDir, '.'))
if ($jarRun.Text) { Write-Output $jarRun.Text }
if ($jarRun.Code -ne 0) { Fail "jar failed with exit code $($jarRun.Code)" }
$jarItem = Get-Item $jarPath
$jarHash = (Get-FileHash -Path $jarPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "jar       : $($jarItem.FullName) ($($jarItem.Length) bytes)"
Write-Output "jar sha256: $jarHash"

$runtimeSummary = 'skipped'
$runtimeBytes = 0
$runtimeFiles = 0
if (-not $NoRuntime) {
  Assert-UnderJavaDir $runtimeDir
  if (Test-Path $runtimeDir) { Remove-Item -Recurse -Force $runtimeDir }
  # Minimal module set: java.base only (no third-party dependencies, no jdk.* modules).
  $jlinkBase = $jvmLocale + @("-J-Xmx$jlinkHeap", '--add-modules', 'java.base', '--strip-debug', '--no-man-pages', '--no-header-files', '--output', $runtimeDir)
  $linked = Invoke-Native $jlink ($jlinkBase + @('--compress=zip-6'))
  if ($linked.Text) { Write-Output $linked.Text }
  if ($linked.Code -ne 0) {
    Write-Output "jlink --compress=zip-6 failed (exit $($linked.Code)); retrying with --compress=2"
    Assert-UnderJavaDir $runtimeDir
    if (Test-Path $runtimeDir) { Remove-Item -Recurse -Force $runtimeDir }
    $linked = Invoke-Native $jlink ($jlinkBase + @('--compress=2'))
    if ($linked.Text) { Write-Output $linked.Text }
    if ($linked.Code -ne 0) { Fail "jlink failed with exit code $($linked.Code)" }
  }
  $runtimeJava = Join-Path $runtimeDir 'bin\java.exe'
  if (-not (Test-Path $runtimeJava)) { Fail "jlink output is missing bin\java.exe: $runtimeDir" }
  $runtimeItems = @(Get-ChildItem -Path $runtimeDir -Recurse -File)
  $runtimeFiles = $runtimeItems.Count
  $runtimeBytes = ($runtimeItems | Measure-Object -Property Length -Sum).Sum
  if (-not $runtimeBytes) { $runtimeBytes = 0 }
  $runtimeSummary = "$runtimeDir ($runtimeBytes bytes, $runtimeFiles file(s))"
  Write-Output "runtime   : $runtimeSummary"
}

$lines = @(
  'vanadium-report build',
  "date      : $((Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz'))",
  "config    : $Configuration",
  "javac     : $javacVersion",
  "javac heap: -J-Xmx$javacHeap",
  "sources   : $($sources.Count)",
  "classes   : $classCount",
  "jar       : $jarPath",
  "jar bytes : $($jarItem.Length)",
  "jar sha256: $jarHash",
  "run flags : $runFlags",
  "runtime   : $runtimeSummary",
  "runtime bytes: $runtimeBytes",
  "runtime files: $runtimeFiles"
)
[IO.File]::WriteAllLines($infoPath, $lines, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "info      : $infoPath"
Write-Output 'BUILD OK'
