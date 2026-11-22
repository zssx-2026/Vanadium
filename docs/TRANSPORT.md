# Native transport (no WinHTTP)

Status: **in progress for 0.5.0**. This document is the contract the kernel implements in
`src/core/remote/net/`; it replaces the WinHTTP-backed transport that shipped up to 0.4.0.

## 1. Why

User directive (2026-11-15): "展开所有需要逆向和依赖库作为代码" — every reverse-engineered behaviour
and every third-party library we lean on must become our own code, and the reverse-engineered download
engine must be realised in source rather than referenced as a black box.

Concretely, until 0.4.0 the whole remote layer went through `winhttp.lib`
(`CMakeLists.txt:62` for `vd_core`, `CMakeLists.txt:111` for the standalone probe). That single
dependency carried: DNS, TCP, TLS, proxy discovery, redirects, chunked decoding and timeouts. The IDM
behaviours documented in `docs/IDM-NOTES.md` were layered on top of it, so we could describe IDM's
strategy but not control the transport that executes it.

0.5.0 removes `winhttp.lib` entirely:

| Layer | 0.4.0 | 0.5.0 |
| --- | --- | --- |
| DNS | WinHTTP | `GetAddrInfoExW` + TTL cache |
| TCP | WinHTTP | Winsock2, non-blocking connect with deadline |
| TLS | WinHTTP (Schannel inside) | Schannel directly (SNI, ALPN, TLS 1.2/1.3) |
| HTTP/1.1 | WinHTTP | own request writer + response parser (chunked, keep-alive) |
| Redirects | WinHTTP | own follower (301/302/303/307/308, ≤ 5 hops, Range preserved) |
| Proxy | WinHTTP automatic | explicit proxy from settings/registry; PAC is **not** implemented |
| Segment engine | our code on WinHTTP | our code on the native stack (unchanged semantics) |

TLS itself is **not** re-implemented: `secur32`/`crypt32` (Schannel) stays, because a hand-written
TLS 1.3 would be a security liability, not a feature. Everything above the record layer is ours.

## 2. Layering

    src/core/remote/net/uri.cpp      URL parse/serialize (scheme, userinfo, host, port, path, query)
    src/core/remote/net/socket.cpp   DNS cache + TCP connect + deadline send/recv
    src/core/remote/net/tls.cpp      Schannel client context (SNI, ALPN, cert chain validation)
    src/core/remote/net/http1.cpp    request writer, response parser, connection pool, redirects
    src/core/remote/transport.cpp    the contract formerly served by winhttp_util.h
    src/core/remote/download.cpp     segment engine (unchanged callers)

`transport.h` keeps the shape the rest of the kernel already uses:

    struct Response { int status; std::string reason; std::map<std::string,std::string> headers;
                      std::string body; std::string final_url; };

    Response request(const HttpOptions& opt);            // buffered GET/HEAD/POST
    void     stream_to_file(const HttpOptions& opt, const std::string& path,
                            bool append, u64* written);  // ranged body → file
    Probe    probe_remote(const std::string& url);       // size, range support, ETag, Last-Modified

`HttpOptions` carries: url, method, headers, body, timeout_ms, max_redirects, proxy, and
`insecure_skip_verify` (reachable only from the test harness, never from a CLI command).

## 3. Socket and TLS rules

- **Deadlines are absolute.** Every socket operation takes a deadline; there is no "infinite" send.
  Default connect 15 s, TLS handshake 20 s, first byte 30 s, inter-read stall 20 s.
- **DNS**: `GetAddrInfoExW` with `OVERLAPPED` + timeout, results cached for 60 s per host, negative
  results cached for 5 s so a dead host cannot stall a segment pool.
- **Connect**: `WSAConnectByNameW` is not used; we resolve, then non-blocking `connect` each address
  with `select` on the remaining deadline, first success wins, `TCP_NODELAY` on.
- **TLS**: `AcquireCredentialsHandleW(SCHANNEL)` cached per process; `InitializeSecurityContextW`
  with `pszTargetName = host` (SNI) and ALPN `http/1.1`; TLS 1.2/1.3 only. Certificate validation
  uses `CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL)` with the hostname, so a wrong-host
  or expired certificate fails the request (a self-signed test server is reachable only through the
  harness flag).
- **No HTTP/2.** ALPN offers `http/1.1` only. IDM ships its own h2 framing; we do not, and a
  mis-negotiated h2 would break Range semantics we rely on.

## 4. HTTP/1.1 rules

- Request line + headers are written in one `send`; `Host` is derived from the URI (port omitted when
  default), `Connection: keep-alive`, `Accept-Encoding: identity` on any ranged request.
- Response parsing is incremental: status line, header block (case-insensitive names, canonicalised
  through the generated header registry), then body by `Transfer-Encoding: chunked`,
  `Content-Length`, or read-to-close.
- **Redirects**: 301/302/303 → GET without body; 307/308 keep method and body. Any hop keeps
  `Range`/`If-Range`. Relative `Location` resolves against the current URL. ≤ 5 hops, else
  `Failure(4002)`.
- **Pool**: key `scheme://host:port`, idle timeout 60 s, max 16 idle connections, one request per
  connection at a time. A connection that saw `Connection: close` or an unparsable body is dropped.
- **Retryable** (at the request layer): connection reset, timeout, 408, 425, 429, 500, 502, 503, 504.
  **Not retryable**: 400, 401, 403, 404, 405, 410, 416, 451 and any TLS validation failure.

## 5. IDM behaviours, now in our source

| IDM evidence (docs/IDM-NOTES.md) | Where it lands in 0.5.0 |
| --- | --- |
| chunk list drives connections (`ChunksList::NeedNewConnection` 0x0030bbac) | `download.cpp` segment queue: idle worker takes the largest remaining block |
| 32-connection ceiling from registry (`MaxConnectionsNumber`) | default 8, cap 16 in CLI, one segment per connection |
| `Range: bytes=0-0` probe + `Content-Range` | `probe_remote` over the native stack |
| `Range: bytes=%I64d-` resume, `If-Range` validation | `download.cpp` resume path sends `If-Range: <ETag|Last-Modified>` |
| `Accept-Encoding: identity` on ranged transfers | request writer sets it for every ranged request |
| three stall restarts then fail | `kSegmentRetries = 3`, backoff 250/500/1000/2000/4000 ms |
| `Retry-After` parsed | honoured for 429/503 when ≤ 30 s, else ignored and logged |
| parts in a temp directory, merged at the end | `<out>.tmp.d<i>` + merge + rename (unchanged) |
| error taxonomy: interrupted / cannot resume / URL changed | `Failure` detail text distinguishes them |

## 6. Acceptance (must all pass before 0.5.0 ships)

1. `Select-String -Pattern 'WinHttp|winhttp.h'` over `src/`, `include/`, `CMakeLists.txt` → no hits.
2. `dumpbin /dependents vanadium.dll` (or `objdump -p`) → no `WINHTTP.dll`; `WS2_32.dll`,
   `CRYPT32.dll`, `SECUR32.dll` present.
3. Local Python server scenarios: chunked body, 302/307 redirect chain, 206 range, mid-body abort,
   1 MB binary — each with a checksum assertion.
4. Real network: `inv download` of `SHA256SUMS.txt` (168 B) and the 22 MB release zip with
   `-thread 4` and `-thread 8`, sha256 equal to the published digest, three consecutive runs.
5. Interrupted-resume test: kill mid-transfer, re-run, resulting sha256 still correct.
6. `inv doctor` prints `transport  native (winsock2 + schannel)` and no WinHTTP version.
7. Full `tests/installer-static.ps1` + ten-step acceptance still PASS on the packaged build.
