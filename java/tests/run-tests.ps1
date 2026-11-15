# End-to-end tests for vanadium-report. Regenerates the fixture, exercises every command,
# both output modes, the three translations and the failure paths, then writes the full
# transcript to java/tests/out.txt (UTF-8, no BOM). The last line is PASS only when every
# check passed; the exit code mirrors that.
# Usage: pwsh java/tests/run-tests.ps1 [-JavaHome <jdk>]
[CmdletBinding()]
param([string]$JavaHome = '')

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$here = $PSScriptRoot
$javaDir = Split-Path -Parent $here
$jar = Join-Path $javaDir 'out\vanadium-report.jar'
$fixture = Join-Path $here 'fixture'
$outFile = Join-Path $here 'out.txt'
$runtimeJava = Join-Path $javaDir 'runtime\bin\java.exe'
$utf8 = New-Object System.Text.UTF8Encoding($false)

$runFlags = @('-Xmx192m', '-XX:ReservedCodeCacheSize=32m', '-XX:TieredStopAtLevel=1', '-Xshare:auto')

$script:log = New-Object System.Collections.Generic.List[string]
$script:checks = 0
$script:fails = 0

function Log([string]$text) {
  $script:log.Add($text)
  Write-Host $text
}

function Section([string]$title) {
  Log ''
  Log ('--- ' + $title)
}

function Ok([string]$name, [bool]$condition, [string]$detail = '') {
  $script:checks++
  if ($condition) {
    Log ('PASS  ' + $name)
  } else {
    $script:fails++
    $suffix = ''
    if ($detail) { $suffix = ' :: ' + $detail }
    Log ('FAIL  ' + $name + $suffix)
  }
}

function Resolve-JavaExe([string]$hint) {
  $candidates = New-Object System.Collections.Generic.List[string]
  if ($hint) { $candidates.Add((Join-Path $hint 'bin\java.exe')) }
  $candidates.Add('C:\Program Files\Java\jdk-25.0.3\bin\java.exe')
  if ($env:JAVA_HOME) { $candidates.Add((Join-Path $env:JAVA_HOME 'bin\java.exe')) }
  $cmd = Get-Command java.exe -ErrorAction SilentlyContinue
  if ($cmd) { $candidates.Add($cmd.Source) }
  foreach ($c in $candidates) {
    if ($c -and (Test-Path $c)) { return $c }
  }
  throw 'java.exe not found; pass -JavaHome <jdk-dir>'
}

function Invoke-Cli {
  param([string[]]$CliArgs, [string]$Exe = '')
  if (-not $Exe) { $Exe = $script:javaExe }
  $flags = $script:runFlags
  $prev = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $raw = & $Exe @flags -jar $script:jar @CliArgs 2>&1
    $code = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $prev
  }
  $text = (@($raw | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine)
  Log ('$ ' + $Exe + ' ' + ($flags -join ' ') + ' -jar ' + (Split-Path -Leaf $script:jar) + ' ' + ($CliArgs -join ' '))
  foreach ($line in ($text -split "\r?\n")) { Log ('    ' + $line) }
  Log ('    [exit ' + $code + ']')
  return [pscustomobject]@{ Output = $text; Exit = $code }
}

function Get-Json($result) {
  try { return ($result.Output | ConvertFrom-Json) } catch { return $null }
}

function Get-LineCount($result) {
  return @($result.Output -split "\r?\n").Count
}

# ---------------------------------------------------------------- setup
$script:javaExe = Resolve-JavaExe $JavaHome
$script:jar = $jar
Log 'vanadium-report test run'
Log ('date      : ' + (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz'))
Log ('java      : ' + $script:javaExe)
Log ('jar       : ' + $jar)
Log ('flags     : ' + ($runFlags -join ' '))
Log ('powershell: ' + $PSVersionTable.PSVersion.ToString())

if (-not (Test-Path $jar)) {
  Log ('FAIL  jar exists :: missing ' + $jar + ' (run java/build.ps1 first)')
  [IO.File]::WriteAllText($outFile, ($script:log -join [Environment]::NewLine) + [Environment]::NewLine + 'FAIL 0 checks' + [Environment]::NewLine, $utf8)
  exit 1
}
Ok 'jar exists' (Test-Path $jar)

$tmp = Join-Path $env:TEMP ('vd-tests-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

try {
  # ------------------------------------------------------------ fixture
  Section 'fixture'
  $fixtureOut = (& (Join-Path $here 'make-fixture.ps1') -OutDir $fixture 2>&1 | Out-String)
  foreach ($line in ($fixtureOut.Trim() -split "\r?\n")) { Log ('    ' + $line) }
  Ok 'fixture generator succeeds' ($fixtureOut -match 'FIXTURE OK') ($fixtureOut.Trim())
  Ok 'fixture has meta.json' (Test-Path (Join-Path $fixture '.inv\meta.json'))
  Ok 'fixture has 8 objects' (@(Get-ChildItem (Join-Path $fixture '.inv\objects') -Recurse -File).Count -eq 8)

  # ------------------------------------------------------------ version
  Section 'version'
  $r = Invoke-Cli @('version')
  Ok 'version exits 0' ($r.Exit -eq 0) ('exit ' + $r.Exit)
  Ok 'version names the tool' ($r.Output -match 'vanadium-report 1\.0\.0')
  Ok 'version reports the JVM' ($r.Output -match 'java 25')

  $r = Invoke-Cli @('version', '--json')
  $j = Get-Json $r
  Ok 'version --json exits 0' ($r.Exit -eq 0)
  Ok 'version --json is one line' ((Get-LineCount $r) -eq 1) ('lines ' + (Get-LineCount $r))
  Ok 'version --json tool' ($j -and $j.tool -eq 'vanadium-report')
  Ok 'version --json version' ($j -and $j.version -eq '1.0.0')

  # ------------------------------------------------------------ report
  Section 'report'
  $report = Invoke-Cli @('report', '--archive', $fixture)
  Ok 'report exits 0' ($report.Exit -eq 0) ('exit ' + $report.Exit)
  Ok 'report prints the title' ($report.Output -match 'Archive report')
  Ok 'report prints the branch tree' ($report.Output -match 'Branch tree' -and $report.Output -match '\+-- /branches/main/dev')
  Ok 'report prints the archive type' ($report.Output -match 'type\s+: nodeonly')
  Ok 'report prints the dedup rate' ($report.Output -match 'dedup rate\s+: 31\.47%')
  Ok 'report lists all four branches' ($report.Output -match 'branches\s+: 4')

  $rj = Invoke-Cli @('report', '--archive', $fixture, '--json')
  $j = Get-Json $rj
  Ok 'report --json exits 0' ($rj.Exit -eq 0)
  Ok 'report --json is one line' ((Get-LineCount $rj) -eq 1) ('lines ' + (Get-LineCount $rj))
  Ok 'report --json branch count' ($j -and @($j.branches).Count -eq 4)
  Ok 'report --json totals.files' ($j -and $j.totals.files -eq 13)
  Ok 'report --json totals.entries' ($j -and $j.totals.entries -eq 15)
  Ok 'report --json dedup_ratio' ($j -and $j.totals.dedup_ratio -gt 0.31)
  Ok 'report --json tree roots' ($j -and @($j.tree).Count -eq 3)

  $rp = Invoke-Cli @('report', '--archive', $fixture, '--json', '--pretty')
  Ok 'report --json --pretty is multi-line' ((Get-LineCount $rp) -gt 10)
  Ok 'report --pretty parses' ($null -ne (Get-Json $rp))

  # ------------------------------------------------------------ audit
  Section 'audit'
  $audit = Invoke-Cli @('audit', '--archive', $fixture)
  Ok 'audit exits 0' ($audit.Exit -eq 0) ('exit ' + $audit.Exit)
  Ok 'audit prints PASS' ($audit.Output -match 'RESULT: PASS')
  Ok 'audit has no missing objects' ($audit.Output -match 'missing objects\s+: 0')
  Ok 'audit has no orphans' ($audit.Output -match 'orphan objects\s+: 0')
  Ok 'audit counts 15 entries' ($audit.Output -match 'entries\s+: 15 \(files 13, dirs 2\)')

  $aj = Invoke-Cli @('audit', '--archive', $fixture, '--json')
  $j = Get-Json $aj
  Ok 'audit --json exits 0' ($aj.Exit -eq 0)
  Ok 'audit --json pass' ($j -and $j.pass -eq $true)
  Ok 'audit --json counts.missing' ($j -and $j.counts.missing -eq 0)
  Ok 'audit --json counts.orphans' ($j -and $j.counts.orphans -eq 0)
  Ok 'audit --json has six checks' ($j -and @($j.checks).Count -eq 6)

  # ------------------------------------------------------------ inspect
  Section 'inspect'
  $ins = Invoke-Cli @('inspect', '--archive', $fixture, '--branch', '/branches/main', '--limit', '3')
  Ok 'inspect exits 0' ($ins.Exit -eq 0) ('exit ' + $ins.Exit)
  Ok 'inspect prints the snapshot' ($ins.Output -match 'seq\s+: 2' -and $ins.Output -match 'add docs')
  Ok 'inspect prints a page footer' ($ins.Output -match 'showing 1-3 of 7')
  Ok 'inspect prints entry rows' ($ins.Output -match 'src/main\.c')

  $ins2 = Invoke-Cli @('inspect', '--archive', $fixture, '--branch', '/branches/main', '--limit', '2', '--offset', '3')
  Ok 'inspect paging offsets' ($ins2.Output -match 'showing 4-5 of 7')

  $ij = Invoke-Cli @('inspect', '--archive', $fixture, '--branch', '/branches/main', '--limit', '2', '--json')
  $j = Get-Json $ij
  Ok 'inspect --json exits 0' ($ij.Exit -eq 0)
  Ok 'inspect --json page.total' ($j -and $j.page.total -eq 7)
  Ok 'inspect --json entry count' ($j -and @($j.entries).Count -eq 2)
  Ok 'inspect --json snapshot seq' ($j -and $j.snapshot.seq -eq 2)
  Ok 'inspect --json branch path' ($j -and $j.branch.path -eq '/branches/main')

  # ------------------------------------------------------------ legacy metadata name
  Section 'legacy metadata name'
  $legacy = Join-Path $tmp 'legacy'
  Copy-Item -Recurse $fixture $legacy
  Move-Item (Join-Path $legacy '.inv\meta.json') (Join-Path $legacy '.inv\archive.json')
  $rl = Invoke-Cli @('report', '--archive', $legacy)
  Ok 'report accepts archive.json' ($rl.Exit -eq 0 -and $rl.Output -match 'name\s+: demo-archive')

  # ------------------------------------------------------------ i18n
  Section 'i18n'
  $zh = Invoke-Cli @('report', '--archive', $fixture, '--lang', 'zh-cn')
  Ok 'zh-cn report title' ($zh.Output -match '存档报告')
  $tr = Invoke-Cli @('audit', '--archive', $fixture, '--lang', 'tr-cn')
  Ok 'tr-cn audit title' ($tr.Output -match '存檔審計')
  $am = Invoke-Cli @('report', '--archive', $fixture, '--lang', 'am-en')
  Ok 'am-en report title' ($am.Output -match 'Archive Report')
  Ok 'default output is ASCII-only' ($report.Output -notmatch '[\u4e00-\u9fff]')
  Ok 'unknown language is rejected' ((Invoke-Cli @('report', '--archive', $fixture, '--lang', 'xx')).Exit -eq 1)

  # ------------------------------------------------------------ error paths
  Section 'error paths'
  $e = Invoke-Cli @('report', '--archive', (Join-Path $tmp 'does-not-exist'))
  Ok 'missing archive exits 1' ($e.Exit -eq 1)
  Ok 'missing archive message' ($e.Output -match '\[error\] archive not found:')

  $e = Invoke-Cli @('report')
  Ok 'missing --archive exits 1' ($e.Exit -eq 1)
  Ok 'missing --archive message' ($e.Output -match '\[error\] --archive <root> is required')

  $e = Invoke-Cli @('inspect', '--archive', $fixture, '--branch', '/nope')
  Ok 'unknown branch exits 1' ($e.Exit -eq 1)
  Ok 'unknown branch message' ($e.Output -match '\[error\] branch not found: /nope')

  $e = Invoke-Cli @('inspect', '--archive', $fixture, '--branch', '/branches/main', '--limit', '0')
  Ok 'bad --limit exits 1' ($e.Exit -eq 1 -and $e.Output -match '\[error\] --limit expects a positive integer')

  $e = Invoke-Cli @('report', '--archive', $fixture, '--bogus')
  Ok 'unknown option exits 1' ($e.Exit -eq 1 -and $e.Output -match '\[error\] unknown option: --bogus')

  $e = Invoke-Cli @('frobnicate', '--archive', $fixture)
  Ok 'unknown command exits 1' ($e.Exit -eq 1 -and $e.Output -match '\[error\] unknown or missing command')

  $corrupt = Join-Path $tmp 'corrupt'
  Copy-Item -Recurse $fixture $corrupt
  [IO.File]::WriteAllText((Join-Path $corrupt '.inv\meta.json'), '{ "format": 1, oops', $utf8)
  $e = Invoke-Cli @('report', '--archive', $corrupt)
  Ok 'corrupt metadata exits 1' ($e.Exit -eq 1)
  Ok 'corrupt metadata message' ($e.Output -match '\[error\] invalid JSON in' -and $e.Output -match 'line 1, column 16')
  Ok 'corrupt metadata has no stack trace' (($e.Output -notmatch 'at inv\.report\.') -and ($e.Output -notmatch 'Exception in thread'))

  $e = Invoke-Cli @('--help')
  Ok 'help exits 0' ($e.Exit -eq 0)
  Ok 'help lists the commands' ($e.Output -match 'Usage:' -and $e.Output -match '--pretty' -and $e.Output -match 'inspect')

  # ------------------------------------------------------------ audit failures
  Section 'audit failures'
  $missing = Join-Path $tmp 'missing-object'
  Copy-Item -Recurse $fixture $missing
  $victim = Get-ChildItem (Join-Path $missing '.inv\objects') -Recurse -File | Select-Object -First 1
  Remove-Item -Force $victim.FullName
  $e = Invoke-Cli @('audit', '--archive', $missing)
  Ok 'missing object fails the audit' ($e.Exit -eq 1)
  Ok 'missing object is reported' ($e.Output -match 'RESULT: FAIL' -and $e.Output -match 'missing objects\s+: 1')

  $ej = Invoke-Cli @('audit', '--archive', $missing, '--json')
  $j = Get-Json $ej
  Ok 'missing object --json pass=false' ($ej.Exit -eq 1 -and $j -and $j.pass -eq $false)
  Ok 'missing object --json count' ($j -and $j.counts.missing -eq 1)

  $badsha = Join-Path $tmp 'bad-sha'
  Copy-Item -Recurse $fixture $badsha
  $manifest = Join-Path $badsha '.inv\branches\b-feature\manifest.json'
  $text = [IO.File]::ReadAllText($manifest)
  $text = $text -replace '"sha":\s*"[0-9a-f]{64}"', '"sha": "not-a-sha"'
  [IO.File]::WriteAllText($manifest, $text, $utf8)
  $e = Invoke-Cli @('audit', '--archive', $badsha)
  Ok 'bad sha256 fails the audit' ($e.Exit -eq 1)
  Ok 'bad sha256 is counted' ($e.Output -match 'bad sha256\s+: 3')

  $orphan = Join-Path $tmp 'orphan'
  Copy-Item -Recurse $fixture $orphan
  $orphanDir = Join-Path $orphan '.inv\objects\ab'
  New-Item -ItemType Directory -Force -Path $orphanDir | Out-Null
  [IO.File]::WriteAllText((Join-Path $orphanDir ('ab' + ('c' * 62))), 'orphan payload', $utf8)
  $e = Invoke-Cli @('audit', '--archive', $orphan)
  Ok 'orphan object does not fail the audit' ($e.Exit -eq 0)
  Ok 'orphan object is counted' ($e.Output -match 'orphan objects\s+: 1')
  $e = Invoke-Cli @('audit', '--archive', $orphan, '--strict')
  Ok 'strict mode fails on orphans' ($e.Exit -eq 1)

  # ------------------------------------------------------------ jlink runtime
  Section 'jlink runtime'
  if (Test-Path $runtimeJava) {
    $rv = Invoke-Cli @('version') $runtimeJava
    Ok 'runtime java runs the jar' ($rv.Exit -eq 0 -and $rv.Output -match 'vanadium-report 1\.0\.0')
  } else {
    Log ('SKIP  runtime java not built yet: ' + $runtimeJava)
  }
} catch {
  Log ''
  Log ('ERROR ' + $_.Exception.Message)
  $script:fails++
} finally {
  if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
}

# ---------------------------------------------------------------- verdict
Log ''
Log ('checks: ' + $script:checks + ', failures: ' + $script:fails)
if ($script:fails -eq 0) {
  Log 'PASS'
  [IO.File]::WriteAllText($outFile, ($script:log -join [Environment]::NewLine) + [Environment]::NewLine, $utf8)
  exit 0
}
Log ('FAIL ' + $script:fails + ' check(s) failed')
[IO.File]::WriteAllText($outFile, ($script:log -join [Environment]::NewLine) + [Environment]::NewLine, $utf8)
exit 1