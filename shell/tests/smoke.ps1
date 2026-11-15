<#
.SYNOPSIS
    Smoke test for the inv Rust shell.

.DESCRIPTION
    Three layers, all headless (no window, no dialog):
      1. shell-only paths that need no kernel: --json/--lang parsing, usage error,
         and the exact "Vanadium kernel not found." failure (exit code 2);
      2. DLL resolution order, including the ..\lib fallback and an ABI-invalid DLL;
      3. rendering / prompting / exit codes against tests\stub\stub.cpp, a test
         double that implements the vd.h ABI and emits scripted events.

    Prints PASS/FAIL per case and a final verdict line; exits non-zero on failure.

.PARAMETER Rebuild
    Force a rebuild of inv.exe (and of the stub DLL).

.PARAMETER Keep
    Keep the temporary directory for inspection.

.EXAMPLE
    pwsh -File shell\tests\smoke.ps1
    pwsh -File shell\tests\smoke.ps1 -Rebuild
#>
[CmdletBinding()]
param(
    [switch]$Rebuild,
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'

$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$shellDir = Split-Path -Parent $testsDir
$root = Split-Path -Parent $shellDir
$invExe = Join-Path $shellDir 'target\release\inv.exe'
$stubSrc = Join-Path $testsDir 'stub\stub.cpp'
$stubOutDir = Join-Path $testsDir 'stub\out'
$stubDll = Join-Path $stubOutDir 'vanadium.dll'
$includeDir = Join-Path $root 'include'

$script:passed = 0
$script:failed = 0
$script:tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('inv_smoke_' + [guid]::NewGuid().ToString('N'))

function Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
    if ($Ok) {
        $script:passed++
        Write-Host ("PASS  {0}" -f $Name)
    } else {
        $script:failed++
        Write-Host ("FAIL  {0}   {1}" -f $Name, $Detail)
    }
}

function Invoke-Inv {
    param(
        [Parameter(Mandatory)][string]$Exe,
        [string[]]$ArgList = @(),
        [string]$StdinFile,
        [string]$Vt,
        [string]$Ascii,
        [string]$Probe,
        [string]$SelfTest,
        [string]$CancelAfter
    )
    $outFile = Join-Path $script:tmp ('o-' + [guid]::NewGuid().ToString('N') + '.txt')
    $errFile = Join-Path $script:tmp ('e-' + [guid]::NewGuid().ToString('N') + '.txt')

    $saved = @{}
    foreach ($name in 'INV_SHELL_VT', 'INV_SHELL_ASCII', 'INV_SHELL_PROBE', 'INV_SHELL_SELFTEST', 'INV_SHELL_CANCEL_AFTER_MS') {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name)
        Remove-Item ("Env:" + $name) -ErrorAction SilentlyContinue
    }
    if ($Vt) { $env:INV_SHELL_VT = $Vt }
    if ($Ascii) { $env:INV_SHELL_ASCII = $Ascii }
    if ($Probe) { $env:INV_SHELL_PROBE = $Probe }
    if ($SelfTest) { $env:INV_SHELL_SELFTEST = $SelfTest }
    if ($CancelAfter) { $env:INV_SHELL_CANCEL_AFTER_MS = $CancelAfter }

    try {
        $params = @{
            FilePath               = $Exe
            NoNewWindow            = $true
            Wait                   = $true
            PassThru               = $true
            RedirectStandardOutput = $outFile
            RedirectStandardError  = $errFile
        }
        if ($ArgList.Count -gt 0) { $params['ArgumentList'] = $ArgList }
        if ($StdinFile) { $params['RedirectStandardInput'] = $StdinFile }

        $proc = Start-Process @params
        $exitCode = $proc.ExitCode
    } finally {
        foreach ($name in $saved.Keys) {
            if ($null -eq $saved[$name]) {
                Remove-Item ("Env:" + $name) -ErrorAction SilentlyContinue
            } else {
                Set-Item ("Env:" + $name) -Value $saved[$name]
            }
        }
    }

    $stdout = if (Test-Path $outFile) { [System.IO.File]::ReadAllText($outFile, [System.Text.Encoding]::UTF8) } else { '' }
    $stderr = if (Test-Path $errFile) { [System.IO.File]::ReadAllText($errFile, [System.Text.Encoding]::UTF8) } else { '' }
    return [pscustomobject]@{ ExitCode = $exitCode; Stdout = $stdout; Stderr = $stderr }
}

function New-StdinFile {
    param([string]$Text)
    $path = Join-Path $script:tmp ('i-' + [guid]::NewGuid().ToString('N') + '.txt')
    if ($null -ne $Text -and $Text.Length -gt 0) {
        [System.IO.File]::WriteAllText($path, $Text, (New-Object System.Text.UTF8Encoding($false)))
    } else {
        [System.IO.File]::WriteAllBytes($path, [byte[]]@())
    }
    return $path
}

New-Item -ItemType Directory -Force -Path $script:tmp | Out-Null
Write-Host ("[smoke] temp dir : {0}" -f $script:tmp)
Write-Host ("[smoke] repo root: {0}" -f $root)

try {
    # ---------------------------------------------------------------- build --
    if ($Rebuild -or -not (Test-Path $invExe)) {
        Write-Host '[smoke] building inv.exe ...'
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $shellDir 'build.ps1') -Configuration Release
        if ($LASTEXITCODE -ne 0) { throw ("build.ps1 failed with exit code {0}" -f $LASTEXITCODE) }
    }
    if (-not (Test-Path $invExe)) { throw "inv.exe not found: $invExe" }

    if ($Rebuild -or -not (Test-Path $stubDll)) {
        Write-Host '[smoke] building stub vanadium.dll ...'
        New-Item -ItemType Directory -Force -Path $stubOutDir | Out-Null
        & g++ -std=c++20 -O1 -shared -static-libgcc -static-libstdc++ -I $includeDir -o $stubDll $stubSrc
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $stubDll)) {
            throw 'stub DLL build failed (g++ not available or compile error)'
        }
    }
    Write-Host ("[smoke] inv.exe  : {0:N0} bytes" -f (Get-Item $invExe).Length)
    Write-Host ("[smoke] stub dll : {0:N0} bytes" -f (Get-Item $stubDll).Length)

    # ------------------------------------------- 1. shell only, no kernel ---
    $bareDir = Join-Path $script:tmp 'bare'
    New-Item -ItemType Directory -Force -Path $bareDir | Out-Null
    $bareExe = Join-Path $bareDir 'inv.exe'
    Copy-Item -LiteralPath $invExe -Destination $bareExe -Force

    $r = Invoke-Inv -Exe $bareExe -ArgList @('version')
    Check 'A1 missing DLL: exit code 2' ($r.ExitCode -eq 2) ("exit=" + $r.ExitCode)
    Check 'A1 missing DLL: exact message' ($r.Stderr.Trim() -eq '[error] Vanadium kernel not found.') ("stderr=" + $r.Stderr.Trim())
    Check 'A1 missing DLL: nothing on stdout' ($r.Stdout.Trim().Length -eq 0) ("stdout=" + $r.Stdout.Trim())

    $r = Invoke-Inv -Exe $bareExe -ArgList @('--json', '--lang', 'zh-cn', 'help') -Probe '1'
    Check 'A2 probe: exit 0 without kernel' ($r.ExitCode -eq 0) ("exit=" + $r.ExitCode)
    Check 'A2 probe: --json parsed' ($r.Stdout -match '"json":true') ("stdout=" + $r.Stdout.Trim())
    Check 'A2 probe: --lang value parsed' ($r.Stdout -match '"lang":"zh-cn"') ("stdout=" + $r.Stdout.Trim())
    Check 'A2 probe: command forwarded' ($r.Stdout -match '"command":\["help"\]') ("stdout=" + $r.Stdout.Trim())
    Check 'A2 probe: three DLL candidates' ($r.Stdout -match 'vanadium\.dll.*vanadium\.dll.*vanadium\.dll') ("stdout=" + $r.Stdout.Trim())

    $r = Invoke-Inv -Exe $bareExe -ArgList @('--lang=tr-cn', 'list') -Probe '1'
    Check 'A3 probe: --lang=value form' ($r.Stdout -match '"lang":"tr-cn"') ("stdout=" + $r.Stdout.Trim())
    Check 'A3 probe: command list' ($r.Stdout -match '"command":\["list"\]') ("stdout=" + $r.Stdout.Trim())

    $r = Invoke-Inv -Exe $bareExe -ArgList @('--lang')
    Check 'A4 --lang without value: exit 1' ($r.ExitCode -eq 1) ("exit=" + $r.ExitCode)
    Check 'A4 --lang without value: usage on stderr' ($r.Stderr -match 'usage: inv') ("stderr=" + $r.Stderr.Trim())

    $r = Invoke-Inv -Exe $bareExe -ArgList @('--json', 'x')
    Check 'A4b unknown flag reaches the kernel loader, not the parser' ($r.ExitCode -eq 2) ("exit=" + $r.ExitCode)

    # --------------------------------------- 2. DLL search order / ABI ------
    $libDir = Join-Path $script:tmp 'layout'
    $libBin = Join-Path $libDir 'bin'
    $libLib = Join-Path $libDir 'lib'
    New-Item -ItemType Directory -Force -Path $libBin, $libLib | Out-Null
    Copy-Item -LiteralPath $invExe -Destination (Join-Path $libBin 'inv.exe') -Force
    Copy-Item -LiteralPath $stubDll -Destination (Join-Path $libLib 'vanadium.dll') -Force
    $r = Invoke-Inv -Exe (Join-Path $libBin 'inv.exe') -ArgList @('version')
    Check 'B1 ..\lib fallback finds the DLL' ($r.ExitCode -eq 0 -and $r.Stdout -match 'vanadium stub') ("exit=" + $r.ExitCode + " stdout=" + $r.Stdout.Trim())

    $badDir = Join-Path $script:tmp 'baddll'
    New-Item -ItemType Directory -Force -Path $badDir | Out-Null
    Copy-Item -LiteralPath $invExe -Destination (Join-Path $badDir 'inv.exe') -Force
    Copy-Item -LiteralPath (Join-Path $env:WINDIR 'System32\kernel32.dll') -Destination (Join-Path $badDir 'vanadium.dll') -Force
    $r = Invoke-Inv -Exe (Join-Path $badDir 'inv.exe') -ArgList @('version')
    Check 'B2 DLL without the ABI: exit 2' ($r.ExitCode -eq 2) ("exit=" + $r.ExitCode)
    Check 'B2 DLL without the ABI: names the missing export' ($r.Stderr -match 'missing export vd_create') ("stderr=" + $r.Stderr.Trim())

    # --------------------------------------------- 3. stub kernel scenarios -
    $runDir = Join-Path $script:tmp 'run'
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    $runExe = Join-Path $runDir 'inv.exe'
    Copy-Item -LiteralPath $invExe -Destination $runExe -Force
    Copy-Item -LiteralPath $stubDll -Destination (Join-Path $runDir 'vanadium.dll') -Force

    $r = Invoke-Inv -Exe $runExe -ArgList @('version')
    Check 'C1 kernel version line' ($r.ExitCode -eq 0 -and $r.Stdout -match 'inv 0\.1\.0') ("exit=" + $r.ExitCode + " stdout=" + $r.Stdout.Trim())

    $r = Invoke-Inv -Exe $runExe -ArgList @('help')
    Check 'C2 kernel help line' ($r.ExitCode -eq 0 -and $r.Stdout -match 'usage: inv') ("exit=" + $r.ExitCode)

    $r = Invoke-Inv -Exe $runExe -ArgList @('demo') -Vt '0'
    Check 'C3 plain mode: no escape codes' (-not ($r.Stdout -match [regex]::Escape([string][char]27))) 'found ESC in piped output'
    Check 'C3 plain mode: permanent lines' ($r.Stdout -match 'demo: start' -and $r.Stdout -match 'demo: done') ("stdout=" + $r.Stdout)
    Check 'C3 plain mode: progress line on completion' ($r.Stdout -match 'download \[' -and $r.Stdout -match 'ETA:') ("stdout=" + $r.Stdout)
    Check 'C3 plain mode: warn on stderr' ($r.Stderr -match '\[warn\] demo warning line') ("stderr=" + $r.Stderr.Trim())
    Check 'C3 plain mode: summary hidden without --json' (-not ($r.Stdout -match '"files"')) 'summary leaked'

    $r = Invoke-Inv -Exe $runExe -ArgList @('demo', '--json') -Vt '0'
    Check 'C4 --json: summary printed' ($r.Stdout -match '"command":"demo","ok":true,"files":3') ("stdout=" + $r.Stdout)

    $r = Invoke-Inv -Exe $runExe -ArgList @('demo') -Vt '1'
    $esc = [regex]::Escape([string][char]27)
    Check 'C5 vt mode: cursor control emitted' ($r.Stdout -match ($esc + '\[')) 'no escape sequences'
    Check 'C5 vt mode: status animation' ($r.Stdout -match 'Connecting\.\.') ("stdout=" + ($r.Stdout.Substring(0, [Math]::Min(400, $r.Stdout.Length))))
    Check 'C5 vt mode: utf8 bar glyphs' ($r.Stdout -match [regex]::Escape([string][char]0x2588)) 'no block glyph'
    Check 'C5 vt mode: bar format' ($r.Stdout -match 'download \[.{10}\] [0-9]+% [0-9]+/20 [0-9.]+(KB|MB|GB)/s ETA:([0-9]{2}:[0-9]{2}:[0-9]{2}|--:--:--)') ("stdout=" + $r.Stdout)
    Check 'C5 vt mode: live region cleared at exit' ($r.Stdout -match 'demo: done') 'lost permanent line'

    $r = Invoke-Inv -Exe $runExe -ArgList @('demo') -Vt '1' -Ascii '1'
    Check 'C6 ascii fallback bar' ($r.Stdout -match 'download \[[#-]{10}\]') ("stdout=" + $r.Stdout)

    $r = Invoke-Inv -Exe $runExe -ArgList @('prompt') -StdinFile (New-StdinFile ("y" + [char]10)) -Vt '0'
    Check 'C7 prompt accepts y' ($r.ExitCode -eq 0 -and $r.Stdout -match 'answer=yes') ("exit=" + $r.ExitCode + " stdout=" + $r.Stdout.Trim())
    Check 'C7 prompt format' ($r.Stdout -match 'Overwrite existing file\? \[y/n\] \(archive\.json\):') ("stdout=" + $r.Stdout.Trim())

    $r = Invoke-Inv -Exe $runExe -ArgList @('prompt') -StdinFile (New-StdinFile ("NO" + [char]10)) -Vt '0'
    Check 'C8 prompt accepts NO (case insensitive)' ($r.Stdout -match 'answer=no') ("stdout=" + $r.Stdout.Trim())

    $r = Invoke-Inv -Exe $runExe -ArgList @('prompt') -StdinFile (New-StdinFile '') -Vt '0'
    Check 'C9 prompt on EOF answers no' ($r.Stdout -match 'answer=no') ("stdout=" + $r.Stdout.Trim())

    $r = Invoke-Inv -Exe $runExe -ArgList @('prompt') -StdinFile (New-StdinFile ("maybe" + [char]10 + "y" + [char]10)) -Vt '0'
    Check 'C10 prompt: non-tty junk answers no, no re-prompt loop' ($r.Stdout -match 'answer=no' -and ([regex]::Matches($r.Stdout, '\[y/n\]')).Count -eq 1) ("stdout=" + $r.Stdout.Trim())

    $r = Invoke-Inv -Exe $runExe -ArgList @('fail') -Vt '0'
    Check 'C11 error event: exit code propagated' ($r.ExitCode -eq 3) ("exit=" + $r.ExitCode)
    Check 'C11 error event: stderr prefix' ($r.Stderr -match '\[error\] simulated failure \(code 42\)') ("stderr=" + $r.Stderr.Trim())

    $r = Invoke-Inv -Exe $runExe -ArgList @('summary', '--json') -Vt '0'
    Check 'C12 summary --json only' ($r.Stdout -match '"command":"summary"') ("stdout=" + $r.Stdout.Trim())

    $r = Invoke-Inv -Exe $runExe -ArgList @('nosuchcommand') -Vt '0'
    Check 'C13 kernel exit code 1 propagated' ($r.ExitCode -eq 1) ("exit=" + $r.ExitCode)

    $r = Invoke-Inv -Exe $runExe -ArgList @('selftest') -SelfTest 'vectors' -Vt '0'
    Check 'C14 INV_SHELL_SELFTEST path' ($r.ExitCode -eq 0 -and $r.Stdout -match 'selftest ok filter=vectors') ("exit=" + $r.ExitCode + " stdout=" + $r.Stdout.Trim())

    $r = Invoke-Inv -Exe $runExe -ArgList @('--json', 'version') -Vt '0'
    Check 'C15 shell flags do not reach the kernel command' ($r.Stdout -match 'inv 0\.1\.0') ("stdout=" + $r.Stdout.Trim())

    # Ctrl+C: the callback returns -1, the kernel reports the cancellation and the
    # shell exits 130. The flag is raised by the shell's own test hook, so this
    # covers everything except the console handler registration itself.
    $r = Invoke-Inv -Exe $runExe -ArgList @('cancel') -Vt '0' -CancelAfter '250'
    Check 'C16 cancel: exit code 130' ($r.ExitCode -eq 130) ("exit=" + $r.ExitCode)
    Check 'C16 cancel: kernel cancellation notice' ($r.Stdout -match 'Operation cancelled, exit\.') ("stdout=" + $r.Stdout.Trim())
}
finally {
    if (-not $Keep) {
        Remove-Item -Recurse -Force $script:tmp -ErrorAction SilentlyContinue
    } else {
        Write-Host ("[smoke] kept: {0}" -f $script:tmp)
    }
}

Write-Host ''
Write-Host ("SMOKE_RESULT passed={0} failed={1}" -f $script:passed, $script:failed)
if ($script:failed -gt 0) {
    Write-Host 'SMOKE_VERDICT=FAIL'
    exit 1
}
Write-Host 'SMOKE_VERDICT=PASS'
exit 0
