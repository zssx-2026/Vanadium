#!/usr/bin/env python3
"""Count source lines for the Vanadium tree.

Usage:
  python tools/count_lines.py [path ...] [--json out.json] [--top N]

Default paths are src include schema tools. Reports total files, total lines,
largest file and a per-directory breakdown. Generated code is reported
separately so the hand-written part stays visible.
"""
import json
import os
import sys

TEXT_EXT = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".json", ".py", ".md", ".txt", ".cmake"}
SKIP_DIRS = {"__pycache__", ".git", "build", "out", ".vs"}


def iter_files(root):
    for base, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in names:
            if os.path.splitext(name)[1].lower() in TEXT_EXT:
                yield os.path.join(base, name)


def count(path):
    total = 0
    with open(path, "rb") as handle:
        for _ in handle:
            total += 1
    return total


def main(argv):
    paths = []
    out_json = None
    top = 10
    index = 0
    while index < len(argv):
        item = argv[index]
        if item == "--json":
            index += 1
            out_json = argv[index]
        elif item == "--top":
            index += 1
            top = int(argv[index])
        else:
            paths.append(item)
        index += 1
    if not paths:
        paths = ["src", "include", "schema", "tools"]
    rows = []
    for path in paths:
        if os.path.isfile(path):
            rows.append((path, count(path)))
            continue
        if not os.path.isdir(path):
            continue
        for file_path in iter_files(path):
            rows.append((os.path.relpath(file_path).replace(os.sep, "/"), count(file_path)))
    rows.sort(key=lambda row: (-row[1], row[0]))
    total_lines = sum(row[1] for row in rows)
    gen_rows = [row for row in rows if row[0].startswith("src/gen/")]
    hand_rows = [row for row in rows if not row[0].startswith("src/gen/")]
    print("COUNT_FILES=%d" % len(rows))
    print("COUNT_LINES=%d" % total_lines)
    print("COUNT_GEN_FILES=%d" % len(gen_rows))
    print("COUNT_GEN_LINES=%d" % sum(row[1] for row in gen_rows))
    print("COUNT_HAND_FILES=%d" % len(hand_rows))
    print("COUNT_HAND_LINES=%d" % sum(row[1] for row in hand_rows))
    if rows:
        print("COUNT_MAX_FILE=%s:%d" % rows[0])
    print("TOP_FILES")
    for path, lines in rows[:top]:
        print("  %8d  %s" % (lines, path))
    if out_json:
        payload = {
            "files": len(rows),
            "lines": total_lines,
            "gen_files": len(gen_rows),
            "gen_lines": sum(row[1] for row in gen_rows),
            "hand_files": len(hand_rows),
            "hand_lines": sum(row[1] for row in hand_rows),
            "max_file": {"path": rows[0][0], "lines": rows[0][1]} if rows else None,
            "rows": [{"path": path, "lines": lines} for path, lines in rows],
        }
        with open(out_json, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(payload, handle, indent=1, ensure_ascii=False)
            handle.write("\n")
        print("COUNT_JSON=%s" % out_json)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
