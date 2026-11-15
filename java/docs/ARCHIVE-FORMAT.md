# Vanadium archive format, version 1 (reader contract)

Status: **provisional**. The C++ kernel that owns this format does not exist yet, so the
Java sidecar (vanadium-report) defines v1 here. The field names below are a 1:1 mirror of
`include/vanadium/store.h` (`Entry`, `Snapshot`, `BranchInfo`, `ArchiveType`); when the
kernel lands, the kernel wins and this document must be updated to match it.

All JSON is UTF-8, RFC 8259, snake_case, integers in milliseconds / bytes.
Unknown fields are ignored by the reader; they are not an error.

## 1. Directory layout

```
<archive-root>/
  .inv/
    meta.json                     archive metadata (see 2)
    objects/<aa>/<sha256>         content-addressed object store (see 5)
    branches/<branch-id>/
      manifest.json               current snapshot of the branch (see 3)
      history/<seq>.json          one snapshot per committed revision (see 3)
    deltas/                       reserved (kernel side, not read by the sidecar)
    index/                        reserved (kernel side, not read by the sidecar)
    tmp/                          reserved (kernel side, not read by the sidecar)
    locks/                        reserved (kernel side, not read by the sidecar)
```

`--archive <root>` may point at:

* the archive root (the directory that contains `.inv/`), or
* the `.inv/` directory itself, or
* the metadata file itself (`meta.json` or `archive.json`).

## 2. `meta.json`

```json
{
  "format": 1,
  "id": "e6ceee6b574ff7b37dae757afbe4e9e6acba3d97d99ef4a0b7c1025ebaa24570",
  "name": "demo-archive",
  "type": "nodeonly",
  "created_ms": 1763200000000,
  "branches": [ /* BranchInfo, see below */ ]
}
```

| field        | type   | meaning |
|--------------|--------|---------|
| `format`     | int    | format version, currently `1` |
| `id`         | string | archive identifier, hex sha256 (not re-validated) |
| `name`       | string | display name |
| `type`       | string | `full` \| `nodeonly` \| `least` (ArchiveType in store.h); `least` may carry a free-form suffix |
| `created_ms` | int    | creation time, epoch milliseconds |
| `branches`   | array  | branch records, flat list (parents referenced by path) |

Branch record (`BranchInfo`):

| field        | type   | meaning |
|--------------|--------|---------|
| `id`         | string | directory name under `branches/`, safe ASCII (no separators) |
| `path`       | string | logical path, always starts with `/`, e.g. `/branches/main` |
| `name`       | string | leaf name, e.g. `main` |
| `parent`     | string | parent branch path, `/` for a root branch |
| `created_ms` | int    | creation time, epoch milliseconds |
| `head_seq`   | int    | highest committed sequence number on this branch |
| `is_node`    | bool   | true for node branches (container of child branches) |
| `system`     | bool   | true for system branches (e.g. `/releases/tag/*`) |
| `children`   | array  | child branch paths (informational; the tree is rebuilt from `parent`) |

The sidecar rebuilds the tree from `path`/`parent`, so a missing or stale `children`
list never changes the printed tree.

## 3. Branch manifest and history snapshots

`branches/<branch-id>/manifest.json` is the head snapshot of that branch.
`branches/<branch-id>/history/<seq>.json` holds older snapshots with the same shape.

```json
{
  "seq": 2,
  "branch": "/branches/main",
  "time_ms": 1763200100000,
  "message": "add docs",
  "parent_sha": "c33d4609f5a555782d8f2e5f6dea33061d00f1f340b5e798dbd48b53b23eba20",
  "id": "1fafcea015668678f3ef309c1e9b71d9e40aa2598b30ecb45423e06c3dcd94dc",
  "entries": [ /* Entry, see 4 */ ]
}
```

| field        | type   | meaning |
|--------------|--------|---------|
| `seq`        | int    | commit sequence, 1-based, monotonic per branch |
| `branch`     | string | branch path this snapshot belongs to |
| `time_ms`    | int    | commit time, epoch milliseconds |
| `message`    | string | commit message |
| `parent_sha` | string | `id` of the previous snapshot, `""` for the first commit |
| `id`         | string | snapshot identity, hex sha256 |
| `entries`    | array  | full entry list of the snapshot (not a delta) |

A snapshot carries the **whole** tree, sorted by path (directories included as entries).

## 4. Entries

```json
{
  "path": "src/main.c",
  "dir": false,
  "size": 79,
  "sha": "9731869a333461aef5c2b78383e7115125c0d336469be52fb7452cf965494423",
  "crc": "0000000000000000",
  "mtime": 1763200000000,
  "mode": 420
}
```

| field   | type   | meaning |
|---------|--------|---------|
| `path`  | string | archive-relative path, `/` separators, no leading `/`, no `..`, no empty segment |
| `dir`   | bool   | true for a directory entry (then `size` is 0 and `sha` is `""`) |
| `size`  | int    | file size in bytes |
| `sha`   | string | lowercase hex sha256 of the file content; empty for directories |
| `crc`   | string | CRC64 as 16 hex digits; `"0000000000000000"` means "not recorded" |
| `mtime` | int    | modification time, epoch milliseconds |
| `mode`  | int    | POSIX mode as a **number**: `420` = `0644`, `16877` = `0755` for directories |

The sidecar prints `mode` as octal (`000644`, `040755`); it accepts either a number or a
string on input.

## 5. Object store and dedup rate

Object bytes live at `objects/<first two hex chars>/<full sha256>`. The file name must equal
the entry `sha` and the content hash of the file.

Dedup rate reported by `report`:

```
dedup = 1 - object_bytes / referenced_bytes
```

* `object_bytes` — sum of the sizes of every object present in `objects/`.
* `referenced_bytes` — sum of `size` over all **file** entries of every branch head
  manifest (directories excluded). The same content referenced by N branches counts N times.

Example from the shipped fixture: 8 objects / 355 B stored, 13 file references / 518 B
referenced → `1 - 355/518 = 0.3147` → `31.47%`.

## 6. Tolerant reading rules

The reader accepts these aliases so the kernel can evolve without breaking the sidecar:

* metadata file name: `meta.json` (preferred, matches `Archive::save_meta()`) or
  `archive.json` (as drawn in `docs/DESIGN.md`);
* `branches` may be an array of records **or** an object map `{"<id>": {record}}`;
* entry list may be `entries` or `files`, optionally nested under `"snapshot": {...}`;
* `sha` or `sha256`; `crc` as a number or a hex string; `dir` or `is_dir` or `type: "dir"`;
* `time_ms`/`mtime` as `*_ms` or ISO-8601 strings;
* camelCase variants of the field names above.

A missing or unreadable manifest is not fatal: the branch is reported with
`"manifest": "missing"` and `report` continues; `audit` then fails on the
"manifests readable" check.

## 7. Sidecar CLI contract

```
java -jar vanadium-report.jar <command> [options]

report   --archive <root> [--json] [--pretty]
audit    --archive <root> [--json] [--pretty] [--strict]
inspect  --archive <root> --branch <path> [--limit N] [--offset N] [--json] [--pretty]
version  [--json]
```

Global options: `--lang <en|zh-cn|tr-cn|am-en>`, `--help`.

* `--json` prints **exactly one line** of compact JSON, so the caller can hand it straight to
  `vd_event.a` for a `VD_EV_SUMMARY` event.
* `--pretty` (with `--json`) indents the same document by two spaces; never use it for the
  kernel protocol.
* Exit codes: `0` success, `1` usage / I/O / JSON error, or `audit` result FAIL.
  Errors are printed as one line starting with `[error] ` and never include a stack trace.

Mapping to `include/vanadium/vd.h`:

| sidecar output | kernel event |
|---|---|
| `--json` document (one line) | `VD_EV_SUMMARY`, `a` = that line |
| `[error] <message>` + exit 1 | `VD_EV_ERROR`, `a` = message, `i1` = exit code |
| human text (no `--json`) | `VD_EV_LINE` per line |

## 8. Audit invariants

`audit` checks six things and reports them as `checks[]` with `status` `ok`/`fail`/`warn`:

1. every branch manifest is present and parses;
2. every referenced `sha` exists in the object store;
3. every non-empty `sha` is 64 lowercase hex characters;
4. every entry `path` is relative, `/`-separated, with no empty, `.` or `..` segment;
5. no duplicate `path` inside one snapshot;
6. no orphan objects (present but referenced by no manifest or history) — a warning by
   default, a failure with `--strict`.

`pass` is false when any check other than the orphan warning fails.

## 9. Open points for the kernel team

* Decide `meta.json` vs `archive.json` and delete the other from `docs/DESIGN.md`; the
  sidecar accepts both but the kernel should write exactly one.
* Confirm whether `crc` is CRC64-ECMA or CRC64/XZ; v1 only round-trips the value.
* Confirm the snapshot `id` derivation (currently "hex sha256 of the snapshot payload" in
  the fixture generator only).
* `deltas/`, `index/`, `tmp/`, `locks/` are reserved and not read.
