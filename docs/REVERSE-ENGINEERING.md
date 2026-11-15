# Reverse engineering method record — IDM 6.43.18.3

Subject: `D:\program\Internet Download Manager\IDMan.exe`, 6,506,496 bytes, SHA-256
`6de4b759a6a576156efa68b06dba258f903c140acc84b6bde13db0924b37f4f5`, FileVersion 6,43,18,3,
company Tonec Inc. Purpose: ground the inv remote layer in observable behaviour of the download
manager we are modelling. Findings live in `docs/IDM-NOTES.md`; this file records **how** they were
obtained, so a later reader can re-run or challenge them.

## 0. Scope and authorization

* Local, authorized static analysis of an installed copy on this machine (owner-approved task).
* No patching, no licence circumvention, no unpacking of protected code, no execution of the binary.
* Read-only registry queries; the only files written were our own notes and temp helpers.
* Nothing from IDM is copied into inv: the notes describe behaviour, the implementation was written
  from the public `remote.h` contract.

## 1. Outcome in one paragraph

Full decompilation was **not** possible on this host (Ghidra ran out of heap with 344-512 MB free).
Everything below therefore comes from the static layer: PE headers and import tables, string literals
with file offsets, the live registry, and IDM's own log file. That is enough to establish the chunk
list architecture, the Range/resume protocol, the retry budget, the temp-file layout, the runtime
transport loading, the proxy model and the speed limiter - and not enough to establish the exact
chunk-splitting rule, the resume bookkeeping format or the limiter algorithm, all of which are
marked 未验证 in the notes.

## 2. Tooling and environment

| Tool | State | Note |
| --- | --- | --- |
| REA MCP | **not registered** in this DSH session | `registration:dsh ok=false, config_drift` at `C:\Users\REDMI\.dsh\cordis.patch.yml`; registering requires a DSH restart, out of scope |
| rea-agents CLI | 6.7.0, works | used as the analysis front end |
| Ghidra | 12.1.4 healthy at `D:\dev\DeepSeekHarnessWorkspace\REA\ghidra_12.1.4_PUBLIC` | import OK, analysis OOM |
| Java | 26.0.2 healthy (as reported by doctor) | heap limited via `GHIDRA_HEADLESS_MAXMEM` |
| Hopper / IDA / JADX / Kuna | missing | not available on this host |
| objdump (mingw64) | on PATH | PE headers, imports, delay imports |
| python 3.14 | on PATH | string scanner with offsets |

## 3. Commands and results

### 3.1 Capability check

    npx -y rea-agents@6.7.0 doctor --json

Summary: ghidra healthy (12.1.4), java healthy (26.0.2), hopper/ida/jadx/kuna missing, DSH client
registration `ok=false` with `config_drift`. The plan that would have registered the MCP server
was reviewed and rejected (it edits `cordis.patch.yml` and needs a restart), so the CLI path was used.

### 3.2 Headless decompilation attempt (failed)

    $env:GHIDRA_HEADLESS_MAXMEM = "384M"
    $env:REA_GHIDRA_STARTUP_TIMEOUT_MS = "1200000"
    npx -y rea-agents@6.7.0 analyze "D:\program\Internet Download Manager\IDMan.exe" \
        --provider ghidra --format json

Ghidra imported the PE normally (ghidra.log: TLS callbacks at `100015cc` and `10029728`,
ExportDataDirectory warnings, win32 typeinfo caching) and then failed:

    {"error":"Analysis failed","code":"resource_constraint",
     "message":"Ghidra's operation-scoped process output reports java.lang.OutOfMemoryError.",
     "details":{"operation":"read_binary_inventory_summary",
                "target_sha256":"6de4b759a6a576156efa68b06dba258f903c140acc84b6bde13db0924b37f4f5",
                "provider_version":"12.1.4","exit_code":1}}

    # ghidra.log tail
    # There is insufficient memory for the Java Runtime Environment to continue.

A second java process kept importing after the first failure and pushed free RAM down to 344 MB, at
which point the attempt was abandoned rather than retried with a bigger heap (the host also runs four
Minecraft servers for another team). **No decompiled function bodies exist for this target.**

### 3.3 PE structure

    objdump -p "D:\program\Internet Download Manager\IDMan.exe" > idm_pe.txt
    Select-String idm_pe.txt -Pattern "DLL Name|Delay|LoadLibrary|GetProcAddress"

Result: import table lists exactly 7 DLLs (KERNEL32, USER32, GDI32, COMDLG32, ADVAPI32, COMCTL32,
SHLWAPI); the delay-import directory contains a single null entry; KERNEL32 provides `LoadLibraryExA`
@0x002d81e0, `LoadLibraryW` @0x002d83ec, `LoadLibraryA` @0x002d8410, `GetProcAddress` @0x002d84e0.

### 3.4 Version resource and log file

    (Get-Item "D:\program\Internet Download Manager\IDMan.exe").VersionInfo
    Get-Content "D:\program\Internet Download Manager\GlobalErrors.log"

Version 6,43,18,3 / Tonec Inc. Log (104 bytes) is stale, from an older build:
`v6.42b22, Time: Sep 08 21:49:41.261 2026` then
`ERROR_ALREADY_EXISTS during CM in CDownloaderApp constructor`.

### 3.5 Live registry (read-only)

    Get-ItemProperty "HKCU:\Software\DownloadManager"

Values used in the notes: `MaxConnectionsNumber=0x20` (32), `NewConnType=3`, `TempPath`,
`AppDataIDMFolder`, `CommonAppDataIDMFolder`, `UseHttpProxy=1`, `UseHttpsProxy=1`,
`UseFtpProxy=1`, `bUseSocks=0`, `SocksType=5`, `Socks5ProxyDNS=0`, `nProxyMode=1`,
`EnableDriver=1`, `RememberLastSave=1`, `MonitorUrlClipboard=0`, plus three `REG_NONE` UTF-16
notification strings (`evDownloadComplete`/`evDownloadFailed`/`evQueueStarted`).

### 3.6 String extraction with offsets

    python C:\Users\REDMI\AppData\Local\Temp\idm_kw.py

Scanner: regex for printable ASCII runs >= 4 and UTF-16LE runs >= 4, recording file offsets, then a
keyword filter (Range, chunk, timeout, retry, proxy, speed, User-Agent, temp, connection...). Yields
42,856 ASCII + 1,947 UTF-16 strings for the 6.5 MB image. Every offset quoted in IDM-NOTES.md comes
from this run; the helper is a 60-line script and is not part of the repo.

## 4. Static evidence inventory

| Category | Method | What it produced | Notes section |
| --- | --- | --- | --- |
| Identity | file stat, version resource, REA diagnostics | size, version, SHA-256 | 1 |
| Imports | `objdump -p` | 7 DLLs, empty delay imports, loader imports | 1, 2.6 |
| Range/resume protocol | strings | 4 Range templates, `If-Range`, `Content-Range`, `ETag`, `Last-Modified`, `identity` | 2.3 |
| Chunk engine | strings (mangled symbols + messages) | `ChunksList::*` symbols, "Insert next chunk in list", "The last chunk is full." | 2.1 |
| Concurrency | strings + registry | `MaxConnectionsNumber`, default 32, caution message | 2.2 |
| Stall handling | strings | "Connection restarted because of timeout (1)/(2)/(3)", `retry-after` | 2.4 |
| Temp layout | strings + registry `TempPath` | `%s.tmp`, `%s.tmpv0`, `%s.result.tmp`, `GetTempEndPos` | 2.5 |
| Transport | imports + strings | runtime loading, `CONNECT`, HTTP/2 frame errors, jsproxy | 2.6 |
| User agent | strings + registry `szCustomUserAgent` | `User-Agent: %s`, IE10/Chrome literals | 2.7 |
| Proxy | strings + registry | per-scheme switches, PAC templates, SOCKS5 | 2.8 |
| Speed limiter | strings | `psnSpeedLimit`, `sls_maxSpeed`, "time %ld, speed %ld, downl %I64d Bytes" | 2.9 |
| Error taxonomy | strings | resume-unsupported / cannot-resume / chunk-rebuild messages | 2.10 |
| Registry layout | strings + live query | tree shape, per-download keys, notification values | 2.11 |

## 5. Confidence grading

| Grade | Meaning | Examples |
| --- | --- | --- |
| **A** - directly observed | file/registry/log content read verbatim | size, version, SHA-256, 7-DLL import list, empty delay imports, `MaxConnectionsNumber=32`, `TempPath`, string literals and their offsets, GlobalErrors.log text |
| **B** - structural inference | observation plus one step of reasoning, no decompilation | chunk-list architecture, three-restart budget, purpose of the four Range templates, runtime transport loading, parts written under TempPath, limiter being rate-based |
| **C** - 未验证 | would need disassembly or a live experiment we did not run | chunk split rule, minimum chunk size, resume bookkeeping format, whether `Retry-After` is honoured, HTTP/2 by default, limiter algorithm, per-site connection overrides |

Rule applied: a claim only gets A if the quoted bytes are in the file at the quoted offset; B claims
are phrased as "reading"; C claims are listed in IDM-NOTES.md section 3 and never used as an
implementation requirement.

## 6. Failures and lessons

1. **Ghidra on 6.5 MB with 384 MB heap** - import succeeds, analysis OOMs in
   `read_binary_inventory_summary`. A larger heap was not attempted because the host had under
   512 MB free; the right move on a bigger host is `GHIDRA_HEADLESS_MAXMEM=4G` and a fresh run.
2. **`rea inspect-with-objdump` without `--operation`** - validation error; the operation flag is
   mandatory (`file_headers`/`section_headers`/`symbols`/`relocations`/`dwarf`/`disassemble`).
3. **`inspect-pe-resources`** - output is tens of KB; must be filtered, otherwise it floods the
   context without adding evidence.
4. **No `.dlst`/`.part`/`.dwnl` strings** - a negative result, and the reason the notes say the
   resume state is probably in the registry or `idmfc.dat` rather than in a sidecar file.
5. **Stale GlobalErrors.log** - its version string (6.42b22) does not match the binary (6.43.18.3);
   useful only as proof that the app logs to that file, not as evidence about the current build.

## 7. Reproduction checklist

1. `npx -y rea-agents@6.7.0 doctor --json` - confirm ghidra/java state.
2. `objdump -p` on the binary - imports, delay imports, loader imports.
3. `python idm_kw.py` (or any ASCII/UTF-16LE scanner with offsets) - string evidence.
4. `Get-ItemProperty "HKCU:\Software\DownloadManager"` - live configuration.
5. Optional, only with >= 2 GB free: `GHIDRA_HEADLESS_MAXMEM=4G` + `rea analyze ... --provider ghidra`
   to obtain the function bodies that would turn every C claim into A or B.

## 8. Boundary statement

The binary was never executed for this analysis, no IDM setting was modified, and no attempt was made
to parse `idmfc.dat` or any other opaque state file. Claims about IDM behaviour are descriptive; the
inv implementation was written independently against `include/vanadium/remote.h` and is documented
against these observations in `docs/IDM-NOTES.md` section 7.