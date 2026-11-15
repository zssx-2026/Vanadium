# inv shell (Rust)

`inv.exe` is the console front end for the Vanadium kernel (`vanadium.dll`).
It owns argument parsing, event rendering, prompts, Ctrl+C and the exit code;
all command logic lives in the kernel.

Pure `std`, no third-party crates. Built with the MSVC toolchain, so `link.exe`
must come from the Visual Studio environment — `build.ps1` sets that up.

## Build

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File shell\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File shell\build.ps1 -Dist   # also copies to dist\inv.exe
```

Windows PowerShell 5.1 is the shell on this machine (`pwsh` is not installed),
so every script here is 5.1 compatible.

Output: `shell\target\release\inv.exe`.

## Usage

```
inv [--json] [--lang <code>] <command> ...
```

| flag | meaning |
| --- | --- |
| `--json` | print the kernel's `VD_EV_SUMMARY` JSON (hidden otherwise) |
| `--lang <code>` / `--lang=<code>` | language passed to `vd_create`, e.g. `en`, `zh-cn`, `tr-cn`, `am-en` |
| `--` | end of shell flags; the rest is forwarded verbatim |

`--version` and `--help` are kernel commands: the shell forwards them unchanged
and does no business logic of its own.

## Kernel discovery

`LoadLibraryW` is tried in this order:

1. `<dir of inv.exe>\vanadium.dll`
2. `<dir of inv.exe>\..\lib\vanadium.dll`
3. `vanadium.dll` (the usual DLL search path, i.e. `PATH`)

Exports required: `vd_create`, `vd_run`, `vd_destroy`, `vd_version`,
`vd_self_test`. `vd_abi` is optional.

## Exit codes

| code | meaning |
| --- | --- |
| 0 | command succeeded |
| 1 | kernel returned 1, or a `VD_EV_ERROR` was rendered and the kernel returned 0, or a usage error |
| 2 | `vanadium.dll` not found or missing a required export |
| 3..N | whatever the kernel returned |
| 130 | cancelled with Ctrl+C |

## Rendering

* `VD_EV_LINE` — permanent line.
* `VD_EV_STATUS` — one replaceable line per key, animated `text.` / `text..` / `text...`
  every 300 ms; the animation runs on a 25 ms ticker thread, never on the kernel thread.
* `VD_EV_PROGRESS` — `phase [██████████] 40% 12/30 1.2MB/s ETA:00:00:12`, refreshed at
  most every 100 ms, one line per phase, at most 8 live lines, overflow folded into
  `... +N more phase(s)`.
* `VD_EV_ERROR` — `[error] ...` on stderr, forces a non-zero exit code.
* `VD_EV_WARN` — `[warn] ...` on stderr.
* `VD_EV_PROMPT` — `<question> [y/n] (<hint>): `, reads `y`/`yes` or `n`/`no`
  case-insensitively; EOF or non-interactive stdin answers "no".
* `VD_EV_SUMMARY` — printed only with `--json`.
* `VD_EV_STATUS_CLEAR` / `VD_EV_TITLE` — clear the status line / set the console title.

When stdout is not a console (pipe or file) the renderer degrades: no escape
sequences, no animation, progress printed when it completes (or every 2 s).
A console without virtual-terminal support gets an in-place `\r` status line.

**Throughput:** measured from the byte counter in `i3`. `vd.h` documents `i4` as
flags while `docs/DESIGN.md` calls it a rate; the shell uses `i4` as bytes/s only
when it is `>= 1000` (a flag word is small), so both readings work.

## Ctrl+C

`SetConsoleCtrlHandler` is installed before the kernel loads. The first Ctrl+C
sets a flag and makes the callback return `-1` for cancellable events
(`STATUS`, `PROGRESS`, `PROMPT`) — plain output keeps rendering so the kernel's
own `Operation cancelled, exit.` line is shown. A second Ctrl+C exits 130 at once.

## Test hooks

Environment variables, all off by default:

| variable | effect |
| --- | --- |
| `INV_SHELL_PROBE=1` | print the parsed plan as JSON and exit 0 without loading the kernel |
| `INV_SHELL_VERBOSE=1` | log Ctrl+C handler state, kernel version/abi, and every DLL path tried |
| `INV_SHELL_SELFTEST=<filter>` | run `vd_self_test` instead of a command |
| `INV_SHELL_CANCEL_AFTER_MS=<n>` | raise the cancel flag after n ms (headless Ctrl+C) |
| `INV_SHELL_VT=1\|0` | force virtual-terminal output on/off |
| `INV_SHELL_ASCII=1` | ASCII progress bar (`#`/`-`) instead of block glyphs |
| `INV_HOME=<dir>` | home directory passed to `vd_create` |

## Tests

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File shell\tests\smoke.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File shell\tests\smoke.ps1 -Rebuild -Keep
```

`tests\stub\stub.cpp` is a g++ test double implementing the whole `vd.h` ABI, so
the renderer, prompts, exit codes and cancel path can be verified before the real
kernel exists. The suite covers DLL discovery (including the `..\lib` fallback
and an ABI-invalid DLL), plain/VT/ASCII rendering, prompts, `--json`, and exit
codes. Last run: 43 passed, 0 failed.
