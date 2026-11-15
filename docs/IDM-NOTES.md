# IDM static analysis notes

Target: `D:\program\Internet Download Manager\IDMan.exe` (authorized local reverse engineering of
the download manager we are modelling; no patching, no cracking, no execution of the binary).

Purpose: ground the inv remote layer (`src/core/remote/**`) in observable IDM behaviour instead of
guesswork. Every claim below is tied to a file offset, a PE structure, a registry value or a log line.

## 0. Tooling status (REA)

**REA MCP 未注册；使用 REA CLI（rea-agents@6.7.0）+ Ghidra headless 取证**

The REA MCP tools are not registered in this DSH session (doctor reports
`registration:dsh ok=false, config_drift` at `C:\Users\REDMI\.dsh\cordis.patch.yml`). Registering
them requires editing that file and restarting DSH, which is outside this task. The REA CLI was used
instead:

    npx -y rea-agents@6.7.0 doctor --json
    # ghidra 12.1.4 healthy at D:\dev\DeepSeekHarnessWorkspace\REA\ghidra_12.1.4_PUBLIC

    $env:GHIDRA_HEADLESS_MAXMEM = "384M"
    $env:REA_GHIDRA_STARTUP_TIMEOUT_MS = "1200000"
    npx -y rea-agents@6.7.0 analyze "D:\program\Internet Download Manager\IDMan.exe" \
        --provider ghidra --format json

Ghidra imported the PE (ghidra.log shows TLS callbacks at `100015cc`/`10029728`, ExportDataDirectory
warnings, win32 typeinfo caching) and then died on heap exhaustion:

    {"error":"Analysis failed","code":"resource_constraint",
     "message":"Ghidra's operation-scoped process output reports java.lang.OutOfMemoryError.",
     "details":{"operation":"read_binary_inventory_summary",
                "target_sha256":"6de4b759a6a576156efa68b06dba258f903c140acc84b6bde13db0924b37f4f5",
                "provider_version":"12.1.4","exit_code":1}}

    # ghidra.log tail
    # There is insufficient memory for the Java Runtime Environment to continue.

The host had 344-512 MB free at the time (four Minecraft servers run on the same machine), so a
full decompiler pass is not affordable here. **Consequence: no decompiled evidence exists.** All
findings below come from the static layer (strings with file offsets, PE headers/imports, the
read-only registry, and config/log files); anything that would need the disassembly is marked
**未验证**.

## 1. Target facts

| Fact | Value | Evidence |
| --- | --- | --- |
| Size | 6,506,496 bytes | `Get-Item`; matches REA `target_admission.size` |
| SHA-256 | 6de4b759a6a576156efa68b06dba258f903c140acc84b6bde13db0924b37f4f5 | REA diagnostics |
| Version | 6, 43, 18, 3 (Tonec Inc.) | PE version resource |
| Import table | 7 DLLs: KERNEL32, USER32, GDI32, COMDLG32, ADVAPI32, COMCTL32, SHLWAPI | `objdump -p` |
| Delay imports | none (directory holds a single null entry) | `objdump -p` |
| Loader | `LoadLibraryExA` @0x002d81e0, `LoadLibraryW` @0x002d83ec, `LoadLibraryA` @0x002d8410, `GetProcAddress` @0x002d84e0 | import table |
| Old log | `GlobalErrors.log`: "v6.42b22, Time: Sep 08 21:49:41.261 2026" + "ERROR_ALREADY_EXISTS during CM in CDownloaderApp constructor" | file read (stale, older build) |

Offsets below are file offsets. `A` = ASCII string, `U` = UTF-16LE string.

## 2. Findings

### 2.1 The engine is a chunk list driven by connections

Evidence: `ChunksList::NeedNewConnection` @0x0030bbac, `ChunksList::FindMaxFreeChunk` @0x0030ba88,
`ChunksList::AppendFile` @0x0030be7c, `ChunksList::InsertFromReg` @0x0030bfb8,
`ChunksList::ApplyAlternative` @0x0030c110, `ChunksList::CreateNewItem` @0x0030c330,
`ChunksList::FindChunkWithZipCentralDir` @0x0030ba2c; runtime messages "The last chunk is full."
@0x0030bd30, "Disk is full. Cannot add next chunks." @0x0030bf04, "Insert next chunk in list"
@0x0030c888, "Erase chunk_list." @0x0030c638, "Cannot rebuild downloaded file chunks." @0x002fde10,
"Not enough disk space to rebuild downloaded chunks." @0x0030bbe4.

Reading: IDM models a download as a list of chunks; a connection is opened when a chunk needs one
(`NeedNewConnection`), the free-chunk search picks the next piece of work (`FindMaxFreeChunk`),
chunks can be added while the transfer runs ("Insert next chunk in list"), and the list can be
rebuilt from persisted state (`InsertFromReg`) after a restart. Confidence: high for the structure,
**未验证** for the exact split rule (see 3.1).

### 2.2 Connection ceiling is a registry value, default 32

`MaxConnectionsNumber` @0x00301474; live value in `HKCU\Software\DownloadManager` is
`REG_DWORD 0x20` (32) on this machine. UI: `.?AVCMaxConnNumbers@@` @0x003c6704,
`.?AVCAddMaxConnDlg@@` @0x003c28e8. Advice string: "Please use a high number of connections with
caution. Some servers may have specific limitations for a number of connections" @0x002f1c78.

Reading: the per-file connection count is user-tunable with a 32-connection default; IDM warns that
servers may throttle. Our implementation defaults to 8, caps at 64, and never exceeds one segment
per connection (`download.cpp:23-24`).

### 2.3 Range header templates and resume validation

`Range: bytes=0-%I64d` @0x003113d4, `Range: bytes=0-` @0x003113ec, `Range: bytes=%I64d-`
@0x00311400, `Range: bytes=%I64d-%I64d` @0x00311418; `If-Range:` @0x00316f38, `Content-Range:`
@0x00310a2c, `Last-Modified:` @0x00311510, `ETag:` @0x00310a3c, `Accept-Encoding: identity`
@0x00311288.

Reading: IDM opens with a one-byte style probe (`bytes=0-0` semantics via the `0-%I64d` template),
resumes with an open-ended range (`bytes=%I64d-`), and validates the resume with `If-Range` against
`ETag`/`Last-Modified` so a changed object is not spliced onto old bytes. It also asks for
`identity` encoding on ranged transfers, because byte offsets are meaningless on a compressed body.
Confidence: high (four distinct templates plus the validator headers).

### 2.4 Stall handling: three restarts, then fail

"Connection restarted because of timeout (1)" @0x00311b5c, "(2)" @0x00311b2c, "(3)" @0x00311a76,
"Cannot download the file because of timeout" @0x002fd874, `retry-after` @0x003174ec (plus the
truncated duplicate `retry-afte` @0x0031893c).

Reading: a stalled connection is restarted at most three times (numbered messages) before the
download surfaces a timeout error; the `Retry-After` response header is parsed. **未验证**: whether
IDM sleeps for the advertised `Retry-After` delay or only logs it. Our downloader uses the same
three-attempt budget per segment with exponential backoff (`download.cpp:25, download.cpp:94-117`)
and treats 429/503 as retryable at the request layer (`http.cpp:18-21`).

### 2.5 Parts live in a dedicated temp directory

Live `TempPath` = `D:\ProgramData\InternetDownloadManagerDownloadTemp` (REG_SZ). Strings:
"Temporary directory is required for storing file parts during download." @0x002f7300, `%s.tmp`
@0x0030bc44, `%s.tmpv0` @0x00312830, `%s.result.tmp` @0x00315688, `%s.mp4.tmp` @0x0031577c,
`.ts2mp4-%lu-%d.tmp` (U @0x0036bf0c), "Error during HTTPConnection::GetTempEndPos()" @0x003108cc.

Reading: segments are written under the configured temp path, not next to the destination, and the
`.tmpv0` suffix suggests versioned/rotated temp names. **未验证**: the resume bookkeeping format.
A scan for `.dlst`, `.dwnl`, `.part`, `.tmp.meta` found nothing, so the chunk map is probably
kept in the registry or in `idmfc.dat` (5040 bytes) rather than in a sidecar file. Our implementation
writes `<out>.tmp.d<i>` segments next to the destination and merges into `<out>.tmp` before the
rename (`download.cpp:58-60, download.cpp:194-250`) - a deliberate simplification that keeps the
output directory as the only write location.

### 2.6 Transport stack is resolved at runtime

The PE imports only the 7 DLLs listed in section 1 and has an empty delay-import directory, yet the
binary contains `WinHttpGetProxyForUrl` @0x002ea748, `InternetGetProxyInfo` @0x002ea7c0,
`jsproxy.dll` (U @0x002ea81c), `CONNECT %s:%ld HTTP/1.1` @0x003117b4, `%s %s HTTP/1.1`
@0x003111d0, and HTTP/2 frame error strings around @0x0031d9a0-0x0031e0d8 (HEADERS/CONTINUATION
validation).

Reading: IDM loads winhttp/wininet/ws2_32 with `LoadLibrary` + `GetProcAddress` at runtime and
speaks HTTP/1.1 directly (including proxy `CONNECT` tunnelling) with its own HTTP/2 framing code.
**未验证**: whether HTTP/2 is used by default and which of the three stacks carries the actual
transfer. inv uses WinHTTP statically (`winhttp_util.cpp:45-59`), which gives system proxy support
and TLS for free but no HTTP/2.

### 2.7 User agent is per-download configurable

`User-Agent: %s` @0x003111ec, @0x003117db, @0x00313868; `User-Agent: Shockwave Flash` @0x00313b7c;
literals `Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.1; Trident/6.0)` @0x002dea40 and
`Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko)` (U @0x002ea690);
registry `szCustomUserAgent` @0x002dd640.

Reading: a custom UA is stored per site/download, with IE10 and Chrome-like fallbacks for servers that
filter unknown agents. inv sends `inv/0.1 (+vanadium)` by default and lets callers override it per
request (`HttpOptions::user_agent`, `http.cpp` request path).

### 2.8 Proxy configuration is per scheme, plus PAC and SOCKS5

Registry: `UseHttpProxy=1`, `UseHttpsProxy=1`, `UseFtpProxy=1`, `bUseSocks=0`, `SocksType=5`,
`Socks5ProxyDNS=0`, `nProxyMode=1` (measured). Strings: `UseHttpProxy` @0x002d9e74, `HttpProxy`
@0x002d9e90, `UseHttpsProxy` @0x002d9ed0, `FtpProxy` @0x002d9e3c, `UseFtpProxy` @0x002d9e28,
`Socks5ProxyDNS` @0x002d9da8; PAC handling `%sproxy.pac` @0x002ec674, `lt_p_proxy%ld.pac`
@0x002ec6bc, "Downloading of proxy script failed" @0x002f23b8.

Reading: independent proxy switches per protocol, SOCKS5 with optional remote DNS, and downloaded PAC
scripts with per-language variants. inv relies on WinHTTP automatic proxy (system settings) only
(`winhttp_util.cpp:52-58`); per-scheme overrides are not implemented.

### 2.9 Speed limiter is a measured rate over time windows

`psnSpeedLimit` @0x00314918, `bUseSpeedLimiter` @0x00314928, `sls_maxSpeed` @0x00308268,
"Speed Limiter" @0x002f54a4, "Maximum download speed for one file" (U @0x003e5d86), progress format
`time %ld, speed %ld, downl %I64d Bytes` @0x00314a18.

Reading: the limiter throttles a single file against a configured maximum, and the accounting record
keeps (elapsed time, instantaneous speed, cumulative bytes) - i.e. a token/window based scheme.
inv has no limiter; not needed for the archive use case, recorded here so the omission is deliberate.

### 2.10 Error taxonomy worth mirroring in messages

"The data transfer has been interrupted and the server does not support \"resume\". It's only
possible to download this file from the beginning." @0x002f5838; "Cannot resume downloading the
file:" @0x002f4fd0; "IDM is waiting for the new address to resume the file:" @0x002f4f98;
"Cannot rebuild downloaded file chunks." @0x002fde10.

Reading: IDM distinguishes (a) transport interrupted, (b) server cannot resume, (c) URL changed and
needs re-capture, (d) local chunk state unreadable. inv collapses these into a single
`vd::Failure` with an HTTP status plus body summary (`http.cpp:95-98`); the distinctions are
preserved in the detail text.

### 2.11 Registry layout

`Software\DownloadManager\` @0x002e2b1c, per-download keys `...\%ld` @0x002e48cc, `...\Queue`
@0x002e7298, `...\Scheduler` @0x002e6fc8. Live values: `MaxConnectionsNumber=32`,
`NewConnType=3`, `TempPath`, `AppDataIDMFolder`, `CommonAppDataIDMFolder`, `EnableDriver=1`,
`LaunchOnStart=1`, `RememberLastSave=1`, `MonitorUrlClipboard=0`, `FtpPasive=1`, plus
`evDownloadComplete` / `evDownloadFailed` / `evQueueStarted` (REG_NONE, UTF-16 Chinese
notification text).

Reading: settings, queue state and per-download records all live in one registry tree; notification
texts are stored as raw UTF-16. inv keeps settings in `%APPDATA%\inv\settings.json` and credentials
in `%APPDATA%\inv\credentials.json` (`auth.cpp:12-18`), which is easier to audit and to diff.

## 3. Not proven (未验证)

1. **Chunk split rule.** Whether IDM halves the largest chunk, grows the chunk count gradually, or
   sizes chunks from the remaining time - no decompiled code. Our rule (split the largest block while
   a worker is idle, minimum 256 KiB, split threshold 4 MiB) is a reasonable analogue, not a copy.
2. **Minimum chunk size and the exact per-connection work assignment.**
3. **Resume bookkeeping format** - no sidecar-file strings exist; possibly a registry blob or
   `idmfc.dat`. We do not attempt to read IDM state.
4. **Retry-After handling** - parsed, but whether the delay is honoured is unknown.
5. **HTTP/2 default** and which stack performs the transfer.
6. **Speed limiter algorithm** (token bucket vs. sleep-based pacing).
7. **Per-site connection overrides** (`defexclist.txt`, "set 1 connection for this site in the list
   of exceptions" @0x002f5a34) - file read but not modelled.

## 4. How inv maps the evidence

| IDM behaviour | inv implementation | Where |
| --- | --- | --- |
| One-byte range probe for size + range support | `probe_remote` sends `Range: bytes=0-0` + `identity`, parses `Content-Range` / `Content-Length` / `Accept-Ranges` | `winhttp_util.cpp:370-409` |
| `Range: bytes=%I64d-%I64d` per connection | `Range: bytes=from-to` per segment | `download.cpp:90-91` |
| `Accept-Encoding: identity` on ranged transfers | same, in both range paths | `download.cpp:92`, `http.cpp:202` |
| Three stall restarts | `kSegmentRetries = 3` with 250 ms..4 s backoff | `download.cpp:25, 94-117` |
| 32-connection default ceiling | default 8, cap 64, one segment per connection | `download.cpp:23-24, 313-314` |
| Parts in a temp directory, merged at the end | `<out>.tmp.d<i>` parts + `<out>.tmp` merge + rename | `download.cpp:58-60, 206-247` |
| `If-Range` resume validation | not implemented (fresh downloads only) | - |
| HTTP/2, PAC, per-scheme proxy, speed limiter | not implemented (WinHTTP automatic proxy) | `winhttp_util.cpp:45-59` |

## 5. Reproduction

    # strings with offsets (ASCII >= 4 and UTF-16LE >= 4)
    python C:\Users\REDMI\AppData\Local\Temp\idm_kw.py

    # PE imports, delay imports, loader imports
    objdump -p "D:\program\Internet Download Manager\IDMan.exe" | Select-String "DLL Name|Delay|LoadLibrary|GetProcAddress"

    # live registry (read-only)
    Get-ItemProperty "HKCU:\Software\DownloadManager"

    # version resource
    (Get-Item "D:\program\Internet Download Manager\IDMan.exe").VersionInfo

    # Ghidra attempt (fails on this host: out of memory)
    $env:GHIDRA_HEADLESS_MAXMEM = "384M"
    npx -y rea-agents@6.7.0 analyze "D:\program\Internet Download Manager\IDMan.exe" --provider ghidra --format json

## 6. Self-check of the inv side

The probe that exercises the implementation (14 checks, real network) is described in the module
header of `src/core/remote/probe.cpp`. It verifies the probe/range/merge/content-match behaviour
against `https://proof.ovh.net/files/1Mb.dat` and the GitHub API; it is not part of the main build.

## 7. 实现映射与差异（inv 取舍说明）

第 4 节是粗粒度映射；本节按「IDM 行为 → inv 实现位置（文件:行）→ 是否对齐 → 原因」逐条覆盖
6 处已知差异，便于后续做断点续传 / 代理 / 限速时直接定位改动点。

| # | IDM 行为（证据） | inv 实现位置 | 对齐 | 原因 / 差异 |
| --- | --- | --- | --- | --- |
| 1 | 续传用 `If-Range` 对 `ETag`/`Last-Modified` 校验，对象变了就不拼接（2.3） | `src/core/remote/http.cpp:193`（`fetch_range` 只发 `Range`），`src/core/remote/download.cpp:90-91` | 否 | inv 的分段在单次进程内完成，不存在"上次下了一半、这次接着下"的场景，所以没有校验点。**若将来做跨进程续传，必须先补 `If-Range`**，否则对象被替换后会把旧字节拼进新内容，产生静默损坏（比直接失败更糟）。 |
| 2 | 分片落在专用 `TempPath`，命名 `%s.tmp` / `%s.tmpv0` / `%s.result.tmp`，续传簿记疑似在注册表或 `idmfc.dat`（2.5） | `src/core/remote/download.cpp:58-60`（`<out>.tmp.d<i>`）、`download.cpp:194-250`（合并到 `<out>.tmp` 再 rename） | 否（有意简化） | inv 让输出目录成为唯一写入位置：失败时只清自己造的临时文件，不依赖任何全局临时目录，也不会与另一个 inv 实例抢同一个 TempPath。代价是同目录需要约 1×文件大小的额外空间，且没有可跨进程复用的簿记。 |
| 3 | 并发上限是注册表 `MaxConnectionsNumber`（本机 32），UI 可调并警告服务器限流（2.2） | `src/core/remote/download.cpp:23-24`（默认 8 / 上限 64）、`download.cpp:313-314`（clamp）、`download.cpp:324-327`（不超过段数） | 部分 | GitHub release 资产多为几十 MB 级，32 连接对吞吐无收益却更容易触发 429/503；上限保留 64 是给大归档留余量，同时保证连接数不超过实际段数（避免空连接）。 |
| 4 | 运行期 `LoadLibrary` 加载 winhttp/wininet/ws2_32，自带 HTTP/1.1 `CONNECT` 与 HTTP/2 帧校验（2.6） | `src/core/remote/winhttp_util.cpp:45-59`（`WinHttpOpen` 会话缓存）、`winhttp_util.cpp:132`（`WinHttpOpenRequest`） | 否 | 静态链接 WinHTTP 换来系统代理、TLS、证书校验全部由系统负责，无需自写帧层或 socket 循环；代价是没有 HTTP/2（GitHub API 与资产下载在 HTTP/1.1 下实测正常，见第 6 节探针）。 |
| 5 | 代理按 http/https/ftp 分开关，支持 PAC 下载与 SOCKS5（含远端 DNS）（2.8） | `src/core/remote/winhttp_util.cpp:50-53`（`WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY`，失败回退 `DEFAULT_PROXY`） | 否 | 跟随系统（IE/WinHTTP）代理配置已覆盖绝大多数企业环境；分协议开关与 SOCKS5 需要在 WinHTTP 之上自建连接层，与"不引第三方库、只用系统栈"的约束冲突。 |
| 6 | 独立限速器：`psnSpeedLimit` / `sls_maxSpeed`，按时间窗计量实际速率（2.9） | 无对应实现（`download.cpp:124` 的 worker 循环里没有令牌桶） | 否（有意不做） | 归档上传/下载是批处理，限速只会拉长 CI 与发布耗时；单文件限速对"尽快完成"的目标是负收益。若确有需要，最小改动点是在 `download.cpp:124` 的 worker 取段后、`download_segment` 发送前加令牌桶等待。 |

已对齐（无需改动）的行为，便于对照：

| IDM 行为 | inv 实现位置 | 说明 |
| --- | --- | --- |
| `Range: bytes=%I64d-%I64d` 分片 + `Accept-Encoding: identity` | `src/core/remote/download.cpp:90-92` | 分片请求与整段请求都显式要 identity，避免压缩体上的偏移错位 |
| 卡死重连 3 次后报超时 | `src/core/remote/download.cpp:25`、`download.cpp:94-117` | 每段独立 3 次，退避 250 ms 起翻倍到 4 s 上限 |
| 429/503 视为可重试 | `src/core/remote/http.cpp:18-21` | 请求层再叠 `HttpOptions::retries`（默认 2） |
| 分片顺序合并、完成后改名 | `src/core/remote/download.cpp:194-250` | 先校验段偏移连续（发现空洞即失败），再拼接、`fs::rename` |
| 先用 1 字节探测体积与 Range 支持 | `src/core/remote/winhttp_util.cpp:370-409` | 解析 `Content-Range` / `Content-Length` / `Accept-Ranges`，416 时从 `Content-Range: bytes */total` 取总长 |
| 跟随重定向 | `src/core/remote/http.cpp:69-72` | 301/302/303/307/308，303 与 POST 的 301/302 降级为 GET，最多 5 跳 |
| 凭据落盘需原子 | `src/core/remote/auth.cpp:41-54` | 写 `.tmp` 后 `fs::write_file_atomic` 改名，损坏时返回空库不抛错 |

结论：6 处差异里 4 处是"能力缺失"（If-Range、HTTP/2、分协议代理/SOCKS5、限速器），2 处是
"有意简化"（临时目录布局、并发默认值）。前者都有明确的补入点（见表格最后一列），后者不建议
为对齐 IDM 而改。
