# Validator (If-Range) regression for the inv downloader.
# Reproduces "release asset replaced while a part is half received" and asserts
# the merged file equals the NEW object, not a splice of old and new bytes.
# Zero extra dependencies; needs python + curl on PATH (both present on this host).
param(
    [string]$InvDir = (Join-Path $env:TEMP "inv-validator"),
    [int]$PortFlip = 18091,
    [int]$PortNormal = 18092,
    [int]$PortSize = 18093
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$serverScript = Join-Path $repo "tests\range_server.py"

function Start-RangeServer {
    param([int]$Port, [string[]]$Extra)
    # The repo path contains spaces, so the script path must arrive quoted.
    $arguments = @('"' + $serverScript + '"', "--port", "$Port") + $Extra
    $proc = Start-Process -FilePath "python" -ArgumentList $arguments -PassThru -WindowStyle Hidden
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 250
        $probe = curl.exe -s --max-time 2 "http://127.0.0.1:$Port/hash?gen=0"
        if ($probe -and $probe.Length -eq 64) { return $proc }
    }
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    throw "range_server did not start on port $Port"
}

function Stop-RangeServer {
    param($Proc)
    if ($Proc) { Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue }
}

function Get-GenHash {
    param([int]$Port, [int]$Gen)
    return (curl.exe -s --max-time 5 "http://127.0.0.1:$Port/hash?gen=$Gen").Trim()
}

function Get-FileSha {
    param([string]$Path)
    if (Test-Path $Path) { return (Get-FileHash $Path -Algorithm SHA256).Hash.ToLower() }
    return ""
}

New-Item -ItemType Directory -Force -Path $InvDir | Out-Null
Copy-Item (Join-Path $repo "shell\target\release\inv.exe") $InvDir -Force
Copy-Item (Join-Path $repo "build\cmake\vanadium.dll") $InvDir -Force
$exe = Join-Path $InvDir "inv.exe"
$failures = 0

# inv writes warnings to stderr; with ErrorActionPreference=Stop PowerShell 5.1
# turns native stderr into a terminating error, so run it with Continue and keep
# the exit code instead.
function Invoke-Inv {
    param([string]$Url, [string]$Path, [int]$Threads)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & $exe download $Url -path $Path -thread $Threads 2>&1
        $code = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previous }
    return @{ Output = $out; Rc = $code }
}
$cases = @()

# ---- Case 1: object replaced mid-transfer, part must restart from zero ------
$flipFile = Join-Path $InvDir "flip.bin"
Remove-Item $flipFile -ErrorAction SilentlyContinue
$proc = Start-RangeServer -Port $PortFlip -Extra @("--drop-at-range", "2", "--drop-bytes", "50000", "--flip-after-range", "2")
try {
    $run = Invoke-Inv -Url "http://127.0.0.1:$PortFlip/data.bin" -Path $flipFile -Threads 2
    $out = $run.Output
    $rc = $run.Rc
    $log = ($out | Out-String)
    $oldHash = Get-GenHash -Port $PortFlip -Gen 0
    $newHash = Get-GenHash -Port $PortFlip -Gen 1
    $fileHash = Get-FileSha $flipFile
    $ok = ($rc -eq 0) -and ($fileHash -eq $newHash) -and ($fileHash -ne $oldHash) -and ($log -match "validator changed")
    if (-not $ok) { $failures++ }
    $cases += "CASE flip rc=$rc hash_new=$([bool]($fileHash -eq $newHash)) hash_old=$([bool]($fileHash -eq $oldHash)) warn=$([bool]($log -match 'validator changed')) verdict=$(if ($ok) {'PASS'} else {'FAIL'})"
    $cases += "  file=$fileHash"
    $cases += "  new =$newHash"
    $cases += "  old =$oldHash"
    $tail = @($out | Select-Object -Last 5)
    foreach ($line in $tail) { $cases += "  | $line" }
}
finally { Stop-RangeServer $proc }

# ---- Case 2: plain transfer still works (regression) ------------------------
$normalFile = Join-Path $InvDir "normal.bin"
Remove-Item $normalFile -ErrorAction SilentlyContinue
$proc = Start-RangeServer -Port $PortNormal -Extra @()
try {
    $run = Invoke-Inv -Url "http://127.0.0.1:$PortNormal/data.bin" -Path $normalFile -Threads 2
    $out = $run.Output
    $rc = $run.Rc
    $log = ($out | Out-String)
    $expect = Get-GenHash -Port $PortNormal -Gen 0
    $fileHash = Get-FileSha $normalFile
    $ok = ($rc -eq 0) -and ($fileHash -eq $expect) -and ($log -notmatch "validator changed")
    if (-not $ok) { $failures++ }
    $cases += "CASE normal rc=$rc hash_match=$([bool]($fileHash -eq $expect)) warn=$([bool]($log -match 'validator changed')) verdict=$(if ($ok) {'PASS'} else {'FAIL'})"
    $cases += "  file=$fileHash"
    $cases += "  want=$expect"
    $tail = @($out | Select-Object -Last 3)
    foreach ($line in $tail) { $cases += "  | $line" }
}
finally { Stop-RangeServer $proc }

# ---- Case 3: object grew under the resume -> must fail, not splice ---------
$sizeFile = Join-Path $InvDir "size.bin"
Remove-Item $sizeFile -ErrorAction SilentlyContinue
$proc = Start-RangeServer -Port $PortSize -Extra @("--size2", "300000", "--drop-at-range", "2", "--drop-bytes", "50000", "--flip-after-range", "2")
try {
    $run = Invoke-Inv -Url "http://127.0.0.1:$PortSize/data.bin" -Path $sizeFile -Threads 2
    $out = $run.Output
    $rc = $run.Rc
    $log = ($out | Out-String)
    $ok = ($rc -ne 0) -and ($log -match "validator-changed size=300000 expected=200000")
    if (-not $ok) { $failures++ }
    $cases += "CASE size rc=$rc detail=$([bool]($log -match 'validator-changed size=300000 expected=200000')) verdict=$(if ($ok) {'PASS'} else {'FAIL'})"
    $tail = @($out | Select-Object -Last 4)
    foreach ($line in $tail) { $cases += "  | $line" }
}
finally { Stop-RangeServer $proc }

$cases | ForEach-Object { Write-Output $_ }
if ($failures -eq 0) { Write-Output "VALIDATOR_REPRO cases=3 bad=0 verdict=PASS" }
else { Write-Output "VALIDATOR_REPRO cases=3 bad=$failures verdict=FAIL" }
exit $failures
