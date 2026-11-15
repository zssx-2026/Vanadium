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
rename (`download.cpp:111, download.cpp:348-405`) - a deliberate simplification that keeps the
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
| One-byte range probe for size + range support | `probe_remote` sends `Range: bytes=0-0` + `identity`, parses `Content-Range` / `Content-Length` / `Accept-Ranges` / `ETag` / `Last-Modified` | `winhttp_util.cpp:465-511` |
| `Range: bytes=%I64d-%I64d` per connection | `Range: bytes=from-to` per segment (resume: `from+have`) | `download.cpp:189-190` |
| `Accept-Encoding: identity` on ranged transfers | same, in both range paths | `download.cpp:191, 424`, `http.cpp:180` |
| Three stall restarts | `kSegmentRetries = 3` with 250 ms..4 s backoff | `download.cpp:26, 168-265` |
| 32-connection default ceiling | default 8, cap 64, one segment per connection | `download.cpp:24-25, 502-503, 552-554` |
| Parts in a temp directory, merged at the end | `<out>.tmp.d<i>` parts + `<out>.tmp` merge + rename | `download.cpp:111`, `download.cpp:348-405` |
| `If-Range` resume validation | `If-Range: <ETag, else Last-Modified>` on any resumed part; 200 answer restarts the part instead of appending | `download.cpp:141-148, 187-228`, `winhttp_util.cpp:483-484` |
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
| 1 | 续传用 `If-Range` 对 `ETag`/`Last-Modified` 校验，对象变了就不拼接（2.3） | `src/core/remote/winhttp_util.cpp:465-511`（探测解析 ETag/Last-Modified），`download.cpp:141-148`（`if_range_value`/`validator_tag`），`download.cpp:187-228`（段内续传 + 200 重启），`download.cpp:420-447`（单流续传） | 是 | 0.3.0 补齐：只要某段已写入 `have>0` 就带 `If-Range`（优先 ETag 原样含引号，否则 Last-Modified）。206 继续 append；200 说明校验器已变，删掉该段 part、`have=0` 整段重下并告警 `segment N restarted: validator changed`，绝不把 200 的全文件体拼进 part；200 且长度与 `job.total` 不符则直接失败并带 `validator-changed size=… expected=… validator=…`。复现脚本 `tests/validator_repro.ps1` + `tests/range_server.py`（`--flip-after-range`）。 |
| 2 | 分片落在专用 `TempPath`，命名 `%s.tmp` / `%s.tmpv0` / `%s.result.tmp`，续传簿记疑似在注册表或 `idmfc.dat`（2.5） | `src/core/remote/download.cpp:111`（`<out>.tmp.d<i>`）、`download.cpp:348-405`（合并到 `<out>.tmp` 再 rename） | 否（有意简化） | inv 让输出目录成为唯一写入位置：失败时只清自己造的临时文件，不依赖任何全局临时目录，也不会与另一个 inv 实例抢同一个 TempPath。代价是同目录需要约 1×文件大小的额外空间，且没有可跨进程复用的簿记。 |
| 3 | 并发上限是注册表 `MaxConnectionsNumber`（本机 32），UI 可调并警告服务器限流（2.2） | `src/core/remote/download.cpp:23-24`（默认 8 / 上限 64）、`download.cpp:313-314`（clamp）、`download.cpp:324-327`（不超过段数） | 部分 | GitHub release 资产多为几十 MB 级，32 连接对吞吐无收益却更容易触发 429/503；上限保留 64 是给大归档留余量，同时保证连接数不超过实际段数（避免空连接）。 |
| 4 | 运行期 `LoadLibrary` 加载 winhttp/wininet/ws2_32，自带 HTTP/1.1 `CONNECT` 与 HTTP/2 帧校验（2.6） | `src/core/remote/winhttp_util.cpp:45-59`（`WinHttpOpen` 会话缓存）、`winhttp_util.cpp:132`（`WinHttpOpenRequest`） | 否 | 静态链接 WinHTTP 换来系统代理、TLS、证书校验全部由系统负责，无需自写帧层或 socket 循环；代价是没有 HTTP/2（GitHub API 与资产下载在 HTTP/1.1 下实测正常，见第 6 节探针）。 |
| 5 | 代理按 http/https/ftp 分开关，支持 PAC 下载与 SOCKS5（含远端 DNS）（2.8） | `src/core/remote/winhttp_util.cpp:50-53`（`WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY`，失败回退 `DEFAULT_PROXY`） | 否 | 跟随系统（IE/WinHTTP）代理配置已覆盖绝大多数企业环境；分协议开关与 SOCKS5 需要在 WinHTTP 之上自建连接层，与"不引第三方库、只用系统栈"的约束冲突。 |
| 6 | 独立限速器：`psnSpeedLimit` / `sls_maxSpeed`，按时间窗计量实际速率（2.9） | 无对应实现（`download.cpp:124` 的 worker 循环里没有令牌桶） | 否（有意不做） | 归档上传/下载是批处理，限速只会拉长 CI 与发布耗时；单文件限速对"尽快完成"的目标是负收益。若确有需要，最小改动点是在 `download.cpp:124` 的 worker 取段后、`download_segment` 发送前加令牌桶等待。 |

已对齐（无需改动）的行为，便于对照：

| IDM 行为 | inv 实现位置 | 说明 |
| --- | --- | --- |
| `Range: bytes=%I64d-%I64d` 分片 + `Accept-Encoding: identity` | `src/core/remote/download.cpp:189-191` | 分片请求与整段请求都显式要 identity，避免压缩体上的偏移错位 |
| 卡死重连 3 次后报超时 | `src/core/remote/download.cpp:26`、`download.cpp:115-120`、`download.cpp:168-265` | 每段独立 3 次，退避 250 ms 起翻倍到 4 s 上限 |
| 429/503 视为可重试 | `src/core/remote/http.cpp:16-19` | 408/429/500/502/503/504 均重试，再叠 `HttpOptions::retries`（默认 2） |
| 分片顺序合并、完成后改名 | `src/core/remote/download.cpp:348-405` | 先校验段偏移连续（发现空洞即失败），再拼接、`fs::rename` |
| 先用 1 字节探测体积与 Range 支持 | `src/core/remote/winhttp_util.cpp:465-511` | 解析 `Content-Range` / `Content-Length` / `Accept-Ranges`，416 时从 `Content-Range: bytes */total` 取总长 |
| 跟随重定向 | `src/core/remote/http.cpp:41-57` | 301/302/303/307/308，303 与 POST 的 301/302 降级为 GET，最多 5 跳 |
| 凭据落盘需原子 | `src/core/remote/auth.cpp:41-55` | 写 `.tmp` 后 `fs::write_file_atomic` 改名，损坏时返回空库不抛错 |

结论：6 处差异里 3 处仍是"能力缺失"（HTTP/2、分协议代理/SOCKS5、限速器），2 处是
"有意简化"（临时目录布局、并发默认值），1 处（If-Range 续传校验）已在 0.3.0 补齐。
剩余能力缺失都有明确的补入点（见表格最后一列）；有意简化不建议为对齐 IDM 而改。

## 8. REA 逆向证据链（版本 / 关键函数与字符串 / 策略）

### 8.1 工具与证据等级

REA MCP 未在本会话注册（`registration:dsh ok=false config_drift`），因此改用
`rea-agents@6.7.0` CLI + Ghidra 12.1.4 headless；Ghidra 在 384M 堆下 OOM
（`rea analyze` exit 1，JSON 里 `resource_constraint`），**没有任何函数体级反编译证据**。
本节证据只来自三类可复现来源，逐条标注等级：

| 等级 | 含义 | 来源 |
| --- | --- | --- |
| 实测 | 本机运行/读取得到的值 | 文件属性、只读注册表、实际 HTTP 行为 |
| 静态 | PE 文件内的字符串或导入表，偏移可复现 | `strings` 扫描（ASCII≥4 与 UTF-16LE≥4）、`objdump -p` |
| 未验证 | 无直接证据，仅为推断 | 明确标注，不作为实现依据 |

### 8.2 版本与目标事实（实测）

- `FileVersion` = `6,43,18,3`，`ProductVersion` = `6,43,18,3`，公司 `Tonec Inc.`
  （`(Get-Item IDMan.exe).VersionInfo`）。**注意**：口述常说的 "6.5" 与本机文件不符，
  以版本资源为准。
- 大小 6,506,496 B，sha256 `6de4b759a6a576156efa68b06dba258f903c140acc84b6bde13db0924b37f4f5`。
- 目录内另有 `IDMan.exe.BAK`（6,517,104 B，旧版），本文所有偏移均指主文件。

### 8.3 关键证据表

| 结论 | 证据（偏移 / 字符串 / 注册表） | 等级 |
| --- | --- | --- |
| 分段引擎是 chunk 列表 + 连接池，不是固定线程数组 | 符号串 `ChunksList::NeedNewConnection` @0x0030bbac、`FindMaxFreeChunk` @0x0030ba88、`AppendFile` @0x0030be7c、`InsertFromReg` @0x0030bfb8、`ApplyAlternative` @0x0030c110、`CreateNewItem` @0x0030c330 | 静态 |
| 连接上限是注册表值，本机 32 | 字符串 `MaxConnectionsNumber` @0x00301474；`HKCU\Software\DownloadManager` 下 `MaxConnectionsNumber=REG_DWORD 0x20` | 实测 + 静态 |
| 四种 Range 模板 | `Range: bytes=0-%I64d` @0x003113d4、`Range: bytes=0-` @0x003113ec、`Range: bytes=%I64d-` @0x00311400、`Range: bytes=%I64d-%I64d` @0x00311418 | 静态 |
| 续传用 If-Range 校验对象未变 | `If-Range:` @0x00316f38、`ETag:` @0x00310a3c、`Last-Modified:` @0x00311510、`Content-Range:` @0x00310a2c | 静态 |
| 卡死后有限次重连，并读 retry-after | `retry-after` @0x003174ec（另有截断串 `retry-afte` @0x0031893c）；失败文案 `The data transfer has been interrupted and the server does not support "resume"...` @0x002f5838 | 静态（重试次数 3 见 2.4，计数本身未验证） |
| UA 可按下载项覆盖 | `szCustomUserAgent` @0x002dd640、`User-Agent: %s` @0x003111ec、内置 UA 串 `Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.1; Trident/6.0)` @0x002dea40 | 静态 |
| 请求行是 HTTP/1.1，且存在 HTTP/2 帧层 | `%s %s HTTP/1.1` @0x003111d0；HTTP/2 帧错误串区 @0x0031d9a0..0x0031e0d8；无 `keep-alive` 字符串 | 静态（连接是否复用未验证） |
| 临时文件命名族 | `%s.tmp` @0x0030bc44、`%s.tmpv0` @0x00312830、`%s.result.tmp` @0x00315688 | 静态 |
| 代理按协议分开 + PAC + SOCKS5 | `CONNECT %s:%ld HTTP/1.1` @0x003117b4、`WinHttpGetProxyForUrl` @0x002ea748、`InternetGetProxyInfo` @0x002ea7c0；注册表 `UseHttpProxy/UseHttpsProxy/UseFtpProxy/bUseSocks/SocksType` | 实测 + 静态 |
| 独立限速器类 | `psnSpeedLimit` @0x00314918、`bUseSpeedLimiter` @0x00314928、RTTI `.?AVCSpeedLimiter@@` @0x003c7368 | 静态 |
| 传输栈运行期解析 | `objdump -p` 显示 `LoadLibrary`/`GetProcAddress` 导入；`winhttp`/`wininet` 相关串 | 静态 |

### 8.4 对 inv 的影响

- **直接采纳**：Range 模板（四段式）、`Accept-Encoding: identity`、`If-Range` 续传校验。
  前两项自 0.1 起一致；第三项在 0.3.0 补齐（见 §7 第 1 行）。
- **只作参考**：chunk 拆分规则、连接上限 32 —— inv 用"最大块先拆 + 上限 64"的等价策略，
  理由见 §7 第 3 行。
- **不采纳**：HTTP/2、分协议代理/SOCKS5、限速器 —— 与"不引第三方库、只用系统栈"和
  "批处理尽快完成"的约束冲突，理由见 §7 第 4-6 行。

### 8.5 未能取证的项

Ghidra 未跑通，因此 `ChunksList` 各方法**只有符号名，没有函数体**：chunk 拆分阈值、
`If-Range` 比较逻辑、retry-after 是否真正参与退避，均属"未验证"。复现命令见 §5；
若后续要在低内存机器上补做，建议换 8G+ 机器或改用 `analyzeHeadless` 的
`-noanalysis` + 定点反编译，而不是整文件全量分析。

## 9. 环境变量（弱网调优）

弱网下握手本身可能超过 20 s，`download_segmented` 的探测与分段请求因此支持两个
进程级环境变量；CLI 的 help 文案在 `src/commands`（本模块不改），故在此登记：

| 变量 | 默认 | 允许范围 | 读取点 | 作用 |
| --- | --- | --- | --- | --- |
| `INV_HTTP_RESOLVE_MS` | 15000 | [1000, 600000] | `src/core/remote/download.cpp:54`（`tune_ms` @:45-50） | DNS/名字解析预算 |
| `INV_HTTP_CONNECT_MS` | 60000 | [1000, 600000] | `src/core/remote/download.cpp:59` | TCP + TLS 握手预算 |

- 两者写入 `HttpOptions::resolve_ms` / `connect_ms`（声明见
  `include/vanadium/remote.h:26-33`），仅影响下载器的探测与分段请求；普通 API 请求仍用
  派生默认值（`min(timeout_ms, 15s)` / `min(timeout_ms, 30s)`）。
- 小于 1000 的值被忽略（视为未设置）；超过 600000 的值按 600000 截断。
- 实测：`INV_HTTP_CONNECT_MS=2000` 时对 `https://10.255.255.1/probe.bin` 的探测在 12 s 内
  3 次 `error 12002` 后失败，detail 为 `stage=probe attempt=3/3 threads=4 bytes=0`。

## 10. 弱网降级策略（0.3.0 收尾）

三段链路各有自己的预算，任何一段的抖动都不应让整条命令直接失败：

| 阶段 | 预算 | 退避 | 读点 |
| --- | --- | --- | --- |
| 探测（`probe_remote`） | 3 次 × 90 s | 500 / 1500 / 4000 ms | `src/core/remote/download.cpp:33-35` |
| 单流回退（`single_stream`） | 3 次重试 | 250 ms 起翻倍到 4 s | `download.cpp:410-474` |
| 分段（`download_segment`） | 每段 3 次 | 同上 | `download.cpp:25, 94-117` |

- **可重试的 WinHTTP 码**（`winhttp_util.cpp:237-255`）：12002 超时、12007 解析失败、
  12029 连接失败、12030 连接错误、12031 RESEND_REQUEST、12152 无效响应。12152 是本机实测到的
  真实形态——GitHub 资产边缘在探测阶段直接断连，报 `WinHttpReceiveResponse failed | error 12152`。
  证书类（12157/12175）与 4xx 一律不重试，避免把永久错误伪装成抖动。
- **探测失败不再等于下载失败**：3 次探测都失败时打 `warn probe failed (...), falling back to a
  single stream`，转 `single_stream` 用普通 GET 把整文件拉下来，再由 sha256 校验兜底。
  代价是拿不到总长度（进度条显示 `?%`），收益是弱网下仍能完成交付。
- 实测证据（2026-11-15 19:10，本机 GitHub 链路抖动期）：
  - 探测第 1 次 12002、第 2 次成功 → 168 B 文件 `RC=0`、38.2 s、sha256 `c84c6d8c…4c82` 与远端一致。
  - 本地 `probe_fail_server.py`（Range 请求一律断连）→ 3 次 12152 后回退单流，`RC=0`、2.4 s、
    200,000 B、sha256 `9f9a45c0…fe73` 与源文件一致。

