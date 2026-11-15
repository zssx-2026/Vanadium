# Build generated library serially and run the generated self-test.
Set-Location -LiteralPath $PSScriptRoot
Set-Location -LiteralPath (Join-Path $PSScriptRoot "../..")
$objDir = "tools/codegen/obj"
New-Item -ItemType Directory -Force -Path $objDir | Out-Null
$srcs = Get-ChildItem -Path "src/gen" -Filter "*.cpp" | Sort-Object Name
$objs = @()
$bad = 0
$swAll = [Diagnostics.Stopwatch]::StartNew()
foreach ($f in $srcs) {
  $obj = Join-Path $objDir ($f.BaseName + ".o")
  $objs += $obj
  $fresh = (Test-Path $obj) -and ((Get-Item $obj).LastWriteTime -ge $f.LastWriteTime)
  if ($fresh) { continue }
  $errFile = Join-Path $objDir ($f.BaseName + ".err")
  $sw = [Diagnostics.Stopwatch]::StartNew()
  g++ -std=c++20 -O0 -Iinclude -Isrc/gen -c $f.FullName -o $obj 2> $errFile
  $code = $LASTEXITCODE
  $sw.Stop()
  if ($code -ne 0) {
    $bad += 1
    "TUFAIL $($f.Name) ms=$($sw.ElapsedMilliseconds)"
    Get-Content $errFile | Select-Object -First 6
  }
}
$swAll.Stop()
"COMPILED files=$($srcs.Count) failed=$bad ms=$($swAll.ElapsedMilliseconds)"
if ($bad -ne 0) { exit 3 }
$swLink = [Diagnostics.Stopwatch]::StartNew()
g++ -std=c++20 -O0 -Iinclude -Isrc/gen $objs tools/codegen/selftest_main.cpp -o tools/codegen/selftest.exe
$linkCode = $LASTEXITCODE
$swLink.Stop()
"LINK exit=$linkCode ms=$($swLink.ElapsedMilliseconds)"
if ($linkCode -ne 0) { exit 4 }
& ./tools/codegen/selftest.exe
"SELFTEST_EXIT=$LASTEXITCODE"