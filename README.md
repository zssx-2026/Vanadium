# inv — Infinity Version Control System

`inv` is a command-line, integrated version control system built as a thin **shell** over a
large native **kernel** named **Vanadium**.

* Shell: Rust (`inv.exe`) — argument dispatch, terminal rendering, \r progress animation, Ctrl+C.
* Kernel: C++20 (`vanadium.dll`) — content-addressed store, branch tree, delta engine, archive
  types (`full` / `nodeonly` / `least`), SHA256 + CRC64 verification, HTTP(S) client, GitHub
  release protocol, i18n (en / zh-cn / tr-cn / am-en), settings.
* Kernel sidecars: Java (`vanadium-report.jar`) — archive reports and audits, bundled with a
  `jlink` runtime so the installer needs no system JRE.

Installer: NSIS (`inv-setup-<version>.exe`).

## Layout

| Path | Contents |
| --- | --- |
| `include/vanadium` | C ABI and internal headers |
| `src/core` | hand-written kernel: json, hashing, LZ4, store, archive, delta, commands |
| `src/gen` | generated kernel code (Unicode tables, catalogs, protocol codecs, self-test vectors) |
| `shell` | Rust shell |
| `java` | Java sidecar modules |
| `schema` | single source of truth for generated code |
| `tools/codegen` | generator for `src/gen` |
| `installer` | NSIS script |
| `tests` | acceptance tests |

## Build

```powershell
pwsh tools/build.ps1 -Configuration Release
```

Outputs land in `dist/`.
