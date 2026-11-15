# vanadium-report (Java sidecar)

Read-only inspector for Vanadium archives. Pure `java.base`, no third-party dependencies,
targets `--release 21` and is built/tested with JDK 25.0.3. It never writes to an archive.

## Commands

```
java -jar vanadium-report.jar report   --archive <root> [--json] [--pretty]
java -jar vanadium-report.jar audit    --archive <root> [--json] [--pretty] [--strict]
java -jar vanadium-report.jar inspect  --archive <root> --branch <path> [--limit N] [--offset N] [--json]
java -jar vanadium-report.jar version  [--json]
```

* `report` — archive metadata, branch tree, per-branch file/dir/byte counts, object store
  size and dedup rate.
* `audit` — object existence, sha256 format, entry paths, duplicate entries, orphan
  objects; prints `RESULT: PASS`/`FAIL` and exits 1 on failure.
* `inspect` — entries of a branch head snapshot with `--limit`/`--offset` paging.
* `version` — tool, Java and VM versions.

Common options: `--lang <en|zh-cn|tr-cn|am-en>` (also `INV_LANG`/`VD_LANG`, default `en`),
`--help`. `--json` prints one compact line; `--pretty` indents it.

Exit codes: `0` ok, `1` error or audit failure. Errors are one `[error] ...` line, no stack
trace (`INV_DEBUG=1` prints one for debugging).

## Build

```powershell
powershell -ExecutionPolicy Bypass -File java\build.ps1
```

Produces:

* `java/out/vanadium-report.jar` — runnable jar, `Main-Class: inv.report.Main`
* `java/runtime/` — jlink runtime image (`--add-modules java.base`)
* `java/out/build-info.txt` — javac heap, jar sha256, run flags, runtime size

The build is tuned for a low-memory machine and must stay that way:

| step  | flags |
|-------|-------|
| javac | `-J-Xmx256m` (single serial invocation, `--release 21`, `-Xlint:all`) |
| jlink | `-J-Xmx256m --add-modules java.base --strip-debug --no-man-pages --no-header-files --compress=zip-6` |
| run   | `-Xmx192m -XX:ReservedCodeCacheSize=32m -XX:TieredStopAtLevel=1 -Xshare:auto` |

The script prechecks free physical memory and prints a `WARN` and waits (up to 120 s by
default, `-MaxWaitSec`) when less than 200 MB is available, instead of failing silently.
Pass `-JavaHome <jdk-dir>` to pick a specific JDK; otherwise it prefers
`C:\Program Files\Java\jdk-25.0.3`, then `JAVA_HOME`, then `PATH`.

Note: JVM options for the compiler and linker need the `-J` prefix (`-J-Xmx256m`); a bare
`-Xmx256m` is rejected by both tools. `-Xshare` takes a colon, not an equals sign.

## Test

```powershell
powershell -ExecutionPolicy Bypass -File java\tests\run-tests.ps1
```

`make-fixture.ps1` regenerates the deterministic fixture in `java/tests/fixture/` (4
branches, 8 objects, 355 B, one shared file across four branches so dedup is measurable),
then `run-tests.ps1` runs 76 assertions: text and JSON output, paging, the legacy
`archive.json` name, the three non-English catalogs, six error paths, the audit failure
modes (missing object, bad sha256, orphan, `--strict`) and a run through the jlink runtime.
Every command line and output is written to `java/tests/out.txt`; the last line is
`PASS` (exit 0) or `FAIL ...` (exit 1).

`run-tests.ps1` contains non-ASCII literals and must stay UTF-8 **with BOM**: Windows
PowerShell 5.1 reads BOM-less files as ANSI and the language assertions then fail.

## Layout

```
java/
  build.ps1                       build + jlink + memory precheck
  src/main/java/inv/report/
    Main.java                     entry point, error handling, UTF-8 stdout
    Cli.java                      argument parsing, dispatch, exit codes
    Report.java                   report command (text + JSON)
    Audit.java                    audit command (text + JSON)
    Inspect.java                  inspect command (text + JSON)
    ArchiveStore.java             tolerant archive loader (meta/manifest/history/objects)
    Model.java                    Branch / Entry / Snapshot records
    Json.java                     RFC 8259 parser + writer
    I18n.java                     en / zh-cn / tr-cn / am-en catalogs
    Util.java                     sha256 format, sizes, time, branch paths, ids
    AppException.java             user-facing error with message key
  tests/
    make-fixture.ps1              deterministic fixture generator
    run-tests.ps1                 assertion runner -> out.txt
    fixture/                      generated archive fixture (committed for convenience)
    out.txt                       last test transcript
  docs/ARCHIVE-FORMAT.md          v1 archive contract for the C++ kernel team
```

The archive format the reader expects is documented in
[docs/ARCHIVE-FORMAT.md](docs/ARCHIVE-FORMAT.md).
