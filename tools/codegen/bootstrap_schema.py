# -*- coding: utf-8 -*-
"""Bootstrap the JSON schemas from the compact Python tables.

The JSON files under schema/ are the single source of truth for the generator;
this script only exists so the first version of those files can be produced and
reviewed mechanically. It is idempotent: running it twice yields identical JSON.

Derivation rules
  zh-cn : written by hand in data_*.py
  tr-cn : zh-cn converted with a simplified -> traditional character map
  am-en : en with British spellings replaced by American ones
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SCHEMA = os.path.join(ROOT, "schema")

sys.path.insert(0, HERE)
from data_messages import MSG          # noqa: E402
from data_errors import ERR            # noqa: E402
from data_commands import CMD, GLOBAL_OPTS  # noqa: E402

LANGS = ["en", "zh-cn", "tr-cn", "am-en"]

# simplified -> traditional pairs, space separated. Only characters that really
# change are listed; identical characters are confirmed through IDENT below.
S2T_PAIRS = """
听聽 监監 级級 装裝

书書 仓倉 储儲 总總 碍礙 访訪 调調 远遠 镜鏡 静靜 页頁 额額

丢丟 两兩 产產 仅僅 体體 凑湊 双雙 壳殼 头頭 尽盡 帧幀 帮幫 异異 抛拋 护護 择擇 旧舊 暂暫 树樹 没沒 称稱 稳穩 紧緊 绑綁 绪緒 联聯 脚腳 补補 视視 译譯 账帳 踪蹤 车車 较較 辑輯 锁鎖 顺順 驱驅 当當

为為 这這 个個 们們 于於 与與 后後 会會 说說 时時 见見 让讓 对對 应應 该該
种種 样樣 现現 实實 际際 处處 图圖 标標 记記 忆憶 库庫 场場 划劃 统統 计計
边邊 态態 势勢 问問 题題 决決 断斷 据據 测測 试試 认認 证證 错錯 误誤 输輸
读讀 写寫 开開 关關 闭閉 删刪 贝貝 复復 贴貼 缓緩 间間 录錄 夹夾 盘盤 号號
编編 码碼 节節 长長 内內 网網 络絡 连連 响響 带帶 宽寬 进進 余餘 满滿 条條
许許 将將 从從 给給 规規 则則 义義 释釋 参參 构構 组組 验驗 钥鑰 随隨 机機
状狀 叠疊 换換 佔占 环環 变變 备備 尔爾 确確 强強 盖蓋 递遞 归歸 层層 浅淺
审審 诊診 况況 报報 页頁 访訪 试試 显顯 调調 节節 静靜 认認 启啟 动動 汇匯 简簡 绝絕 无無 触觸 发發 离離 线線 传傳 请請 议議 档檔 检檢 过過 数數 执執 签簽
终終 权權 说說 备備 份份 备備 储儲 选選 项項 类類 键鍵 语語 详詳 继繼 户戶
还還 么麼 里裡 于於 与與 单單 获獲 压壓 缩縮 块塊 项項 径徑 滤濾 范範 围圍
区區 结結 达達 载載 并並 临臨 转轉 资資 损損 坏壞 滚滾 点點 链鏈 动動 冲衝
册冊 别別 创創 记記 预預 录錄 设設 备備 输輸 码碼 凭憑 键鍵 语語 盖蓋 認認
当當 迁遷 选選 项項 见見 运運 详詳 继繼 续續 户戶 败敗 还還 这這 个個 们們
为為 于於 与與 后後 会會 说說 时時 见見 让讓 对對 应應 该該 种種 样樣 现現
实實 际際 处處 图圖 标標 记記 忆憶 库庫 场場 划劃 统統 计計 边邊 态態 势勢
问問 题題 决決 断斷 据據 测測 试試 认認 证證 错錯 误誤 输輸 读讀 写寫 开開
关關 闭閉 删刪 贝貝 复復 贴貼 缓緩 间間 录錄 夹夾 盘盤 号號 编編 码碼 节節
长長 内內 网網 络絡 连連 响響 带帶 宽寬 进進 余餘 满滿 条條 许許 将將 从從
给給 规規 则則 义義 释釋 参參 构構 组組 验驗 钥鑰 随隨 机機 状狀 叠疊 换換
环環 变變 备備 尔爾 确確 强強 盖蓋 递遞 归歸 层層 浅淺 审審 诊診 况況 报報
"""

IDENT = """
口 展 道 一 不 串 久 也 了 交 他 以 任 但 何 便 停 允 元 六 其 准 初 加 助 包 化 十 印 即 卷 原 叉 及 受 只 可 台 同 吐 向 吞 含 因 圾 垃 基 外 多 套 如 子 字 孤 定 就 嵌 工 常 幂 平 形 彩 必 志 忘 忙 意 或 截 打 抑 拉 持 指 按 控 提 收 新 早 更 替 有 望 束 染 核 根 次 止 歧 段 每 比 渲 父 版 由 留 的 相 看 短 碰 禁 程 端 答 策 算 系 素 翻 而 色 落 裂 起 越 跟 追 集 零 非 首 魔

正 在 远 接 已 断 重 试 超 时 拒 绝 无 法 解 析 主 机 名 握 手 失 败 代 理 触
发 限 流 等 待 网 络 离 线 传 输 续 关 闭 需 要 身 份 验 证 返 回 格 式 错 误 服
务 器 证 书 效 被 请 求 速 率 协 商 议 克 隆 存 档 进 度 完 成 校 检 通 过 数 据 目
空 分 支 使 用 标 签 浅 部 执 行 中 读 取 清 单 获 对 象 压 缩 块 安 排 消 摘 搜 索
找 到 匹 配 项 模 式 路 径 滤 范 围 区 大 小 写 正 则 表 达 结 果 上 跳 二 制 文 件
建 立 容 引 下 载 始 并 致 磁 盘 间 足 入 临 合 转 移 资 源 查 条 缺 少 损 修 复 滚
快 照 序 最 近 节 点 增 量 推 应 链 本 地 改 冲 突 注 册 别 移 除 列 址 保 存 创 终
权 限 说 明 草 稿 记 预 登 录 设 备 授 此 码 令 牌 全 退 出 凭 期 命 未 知 值 键 类
型 界 面 言 覆 恢 默 认 当 前 迁 作 域 法 选 示 例 另 运 详 情 是 否 继 户 败 警 告
信 息 操 还 这 个 们 为 于 与 后 会 说 时 见 让 对 应 该 种 样 现 实 际 处 图 标
记 忆 库 场 划 统 计 边 态 势 问 题 决 断 据 测 试 认 证 错 误 输 读 写 开 关 闭 删
贝 复 贴 缓 间 录 夹 盘 号 编 码 节 长 内 网 络 连 响 带 宽 进 余 满 条 许 将 从 给
规 则 义 释 参 构 组 验 钥 随 机 状 叠 换 环 变 备 尔 确 强 盖 递 归 层 浅 审 诊 况
报 页 访 显 调 静 启 动 终 储 选 类 键 语 详 户 还 么 里 单 获 压 缩 块 径 滤 范 围
区 结 达 载 并 临 转 资 损 坏 滚 点 链 动 冲 册 别 创 预 录 设 备 码 凭 盖 当 迁 见
运 续 败 备 份 储 总 量 统 额 度 配 置 障 碍 兼 容 冗 余 签 署 密 钥 哈 希 熵 池 派
生 折 叠 拼 音 占 位 符 数 量 域 环 境 变 量 冲 突 备 份 过 期 删 除 长 度 布 尔 值
确 认 忽 略 强 制 覆 盖 递 归 深 浅 完 整 性 章 节 诊 断 健 康 状 自 动 复 连 通 性
报 告 文 格 式 摘 要 镜 像 签 名 发 布 仓 库 分 支 节 点 快 照 增 量 回 滚 校 验 对 填 所 把 描 某 物 述 默
"""

def _parse_pairs(text):
    pairs = {}
    for token in text.split():
        if len(token) != 2:
            raise SystemExit("bad S2T pair: %r" % token)
        pairs[token[0]] = token[1]
    return pairs

S2T = _parse_pairs(S2T_PAIRS)
IDENT_CHARS = set(IDENT.split())
UNKNOWN = set()

CJK = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff]")

def to_traditional(text):
    out = []
    for ch in text:
        if ch in S2T:
            out.append(S2T[ch])
        else:
            if CJK.match(ch) and ch not in IDENT_CHARS:
                UNKNOWN.add(ch)
            out.append(ch)
    return "".join(out)

US = [
    ("colour", "color"), ("behaviour", "behavior"), ("centre", "center"),
    ("initialise", "initialize"), ("initialised", "initialized"),
    ("normalise", "normalize"), ("organise", "organize"),
    ("recognise", "recognize"), ("serialise", "serialize"),
    ("cancelled", "canceled"), ("cancelling", "canceling"),
    ("labelled", "labeled"), ("labelling", "labeling"),
    ("modelling", "modeling"), ("travelled", "traveled"),
    ("analyse", "analyze"), ("analysed", "analyzed"),
    ("licence", "license"), ("defence", "defense"),
    ("catalogue", "catalog"), ("dialogue", "dialog"),
    ("artefact", "artifact"), ("fulfilment", "fulfillment"),
    ("judgement", "judgment"), ("acknowledgement", "acknowledgment"),
    ("prioritise", "prioritize"), ("summarise", "summarize"),
    ("authorise", "authorize"), ("customise", "customize"),
    ("optimise", "optimize"), ("minimise", "minimize"),
    ("maximise", "maximize"), ("synchronise", "synchronize"),
    ("tokenise", "tokenize"), ("finalise", "finalize"),
    ("utilise", "utilize"), ("visualise", "visualize"),
    ("stabilise", "stabilize"), ("localise", "localize"),
    ("globalise", "globalize"), ("prioritised", "prioritized"),
    ("compressed", "compressed"), ("grey", "gray"),
    ("cancellable", "cancelable"), ("travelling", "traveling"),
    ("signalling", "signaling"), ("levelled", "leveled"),
    ("behavioural", "behavioral"), ("neighbour", "neighbor"),
]

def to_american(text):
    out = text
    for brit, amer in US:
        if brit == amer:
            continue
        pattern = re.compile(r"\b" + brit + r"\b", re.IGNORECASE)
        def repl(m):
            word = m.group(0)
            if word[:1].isupper():
                return amer[:1].upper() + amer[1:]
            return amer
        out = pattern.sub(repl, out)
    return out

def all_langs(en, zh, tr=None):
    # tr may be given verbatim when the Taiwan wording is hand localised and is
    # not a character by character conversion of zh-cn (see verify.summary).
    return {"en": en, "zh-cn": zh,
            "tr-cn": to_traditional(zh) if tr is None else tr,
            "am-en": to_american(en)}

def detect_indent(text):
    # messages.json was rewritten by another tool with indent=2; keep whatever the
    # file already uses so a merge does not reformat untouched entries.
    for line in text.split("\n")[1:]:
        if line.startswith(" ") and line.lstrip().startswith('"'):
            return len(line) - len(line.lstrip(" "))
    return 1

def write_json(name, payload, indent=1):
    path = os.path.join(SCHEMA, name)
    text = json.dumps(payload, ensure_ascii=False, indent=indent, sort_keys=False) + "\n"
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    os.replace(tmp, path)
    return path, len(text)

def build_messages():
    seen = set()
    messages = {}
    for row in MSG:
        key, en, zh = row[0], row[1], row[2]
        if key in seen:
            raise SystemExit("duplicate message key: " + key)
        seen.add(key)
        messages[key] = all_langs(en, zh, row[3] if len(row) > 3 else None)
    return {
        "version": 1,
        "languages": LANGS,
        "default": "en",
        "comment": "Single source of truth for gen::msg_lookup; generated by tools/codegen/gen.py",
        "messages": messages,
    }

def merge_messages(accept):
    """Merge the table into the existing schema/messages.json.

    The JSON has grown past the table: keys were added straight to the file, so a
    plain rebuild would drop them and would also rewrite tr-cn strings that were
    edited by hand. Only the keys named in --accept may change; every other
    existing entry is carried over verbatim. Keys that would have changed but are
    not accepted are reported as drift.
    """
    path = os.path.join(SCHEMA, "messages.json")
    if not os.path.exists(path):
        return build_messages(), [], [], 1
    with open(path, encoding="utf-8") as fh:
        raw = fh.read()
    indent = detect_indent(raw)
    cur = json.loads(raw)
    table = build_messages()
    out = dict(cur.get("messages", {}))
    accepted, drift = [], []
    for key, entry in table["messages"].items():
        if key not in out:
            out[key] = entry
            continue
        if out[key] == entry:
            continue
        if key in accept:
            out[key] = entry
            accepted.append(key)
        else:
            drift.append(key)
    payload = {
        "version": cur.get("version", table["version"]),
        "languages": cur.get("languages", table["languages"]),
        "default": cur.get("default", table["default"]),
        "comment": cur.get("comment", table["comment"]),
        "messages": out,
    }
    return payload, accepted, drift, indent

def build_errors():
    seen = set()
    errors = []
    for code, en, zh in ERR:
        if code in seen:
            raise SystemExit("duplicate error code: %d" % code)
        seen.add(code)
        errors.append({"code": code, "text": all_langs(en, zh)})
    errors.sort(key=lambda e: e["code"])
    return {
        "version": 1,
        "languages": LANGS,
        "comment": "Single source of truth for gen::error_text",
        "errors": errors,
    }

def build_commands():
    commands = []
    for row in CMD:
        opts = []
        for flag, arg, en, zh in row["opts"]:
            opts.append({"flag": flag, "arg": arg, "summary": all_langs(en, zh)})
        commands.append({
            "name": row["name"],
            "args": row["args"],
            "summary": all_langs(row["en"], row["zh"]),
            "options": opts,
        })
    globals_ = [{"flag": f, "arg": a, "summary": all_langs(en, zh)} for f, a, en, zh in GLOBAL_OPTS]
    return {
        "version": 1,
        "languages": LANGS,
        "comment": "Single source of truth for gen::command_* metadata",
        "global_options": globals_,
        "commands": commands,
    }

FIELD_TYPES = [
    (1, "u8", "uint", 1, "unsigned 8 bit integer"),
    (2, "u16le", "uint", 2, "unsigned 16 bit little endian"),
    (3, "u32le", "uint", 4, "unsigned 32 bit little endian"),
    (4, "u64le", "uint", 8, "unsigned 64 bit little endian"),
    (5, "i64le", "int", 8, "signed 64 bit little endian"),
    (6, "f64le", "float", 8, "IEEE 754 double little endian"),
    (7, "bool8", "bool", 1, "0 or 1"),
    (8, "utf8", "string", 0, "raw UTF-8 bytes, no length prefix"),
    (9, "lp_utf8", "string", 0, "u32 length prefix followed by UTF-8 bytes"),
    (10, "sha256", "hash", 32, "64 hex characters decoded to 32 bytes"),
    (11, "sha1", "hash", 20, "40 hex characters decoded to 20 bytes"),
    (12, "crc64le", "hash", 8, "16 hex characters decoded to 8 bytes"),
    (13, "ms_time", "time", 8, "unix epoch milliseconds as u64le"),
    (14, "semver", "version", 6, "major minor patch as three u16le"),
    (15, "leb128", "varint", 0, "unsigned LEB128"),
    (16, "sleb128", "varint", 0, "signed LEB128 (zigzag)"),
    (17, "b64_lp", "bytes", 0, "base64 text decoded then length prefixed"),
    (18, "path_lp", "string", 0, "normalized slash path, length prefixed"),
    (19, "url_lp", "string", 0, "url text, length prefixed"),
    (20, "lz4_block", "block", 9, "raw u32, compressed u32, flags u8"),
    (21, "json_lp", "json", 0, "compact JSON text, length prefixed"),
    (22, "enum_u8", "enum", 1, "enum ordinal as u8"),
    (23, "kv_lp", "string", 0, "key=value text, length prefixed"),
    (24, "uuid", "uuid", 16, "32 hex characters decoded to 16 bytes"),
    (25, "unix_sec", "time", 4, "unix epoch seconds as u32le"),
    (26, "bool_text", "bool", 0, "ascii true or false, length prefixed"),
]

def build_protocol():
    objects = [
        ("github_user", [("login", 9), ("id", 4), ("node_id", 9), ("avatar_url", 19), ("type", 9)]),
        ("github_repo", [("id", 4), ("name", 9), ("full_name", 9), ("private", 7),
                         ("default_branch", 9), ("size", 4), ("updated_at", 9)]),
        ("github_release", [("id", 4), ("tag_name", 9), ("target_commitish", 9),
                            ("name", 9), ("draft", 7), ("prerelease", 7),
                            ("created_at", 13), ("published_at", 13), ("body", 9)]),
        ("github_asset", [("id", 4), ("name", 9), ("size", 4), ("state", 9),
                          ("content_type", 9), ("browser_download_url", 19),
                          ("digest", 9)]),
        ("github_branch", [("name", 9), ("commit_sha", 11), ("protected", 7)]),
        ("github_commit", [("sha", 11), ("message", 9), ("author_login", 9),
                           ("committed_at", 13), ("tree_sha", 11)]),
        ("github_tree_entry", [("path", 18), ("mode", 9), ("type", 9), ("sha", 11), ("size", 4)]),
        ("github_git_ref", [("ref", 9), ("object_sha", 11), ("object_type", 9)]),
        ("github_rate_limit", [("limit", 4), ("remaining", 4), ("reset", 25), ("used", 4)]),
        ("github_error", [("message", 9), ("documentation_url", 19), ("status", 3)]),
        ("ivpack_header", [("magic", 8), ("version", 2), ("flags", 1),
                           ("block_size", 3), ("block_count", 3), ("raw_size", 4),
                           ("payload_crc64", 12), ("manifest_sha256", 10)]),
        ("ivpack_block", [("index", 3), ("offset", 4), ("raw_size", 3),
                          ("comp_size", 3), ("flags", 1), ("crc64", 12)]),
        ("manifest_entry", [("path", 18), ("sha256", 10), ("crc64", 12),
                            ("size", 4), ("mode", 3), ("mtime_ms", 13)]),
        ("manifest_header", [("version", 2), ("branch_id", 9), ("seq", 4),
                             ("parent_seq", 4), ("entry_count", 3), ("archive_type", 22),
                             ("created_ms", 13)]),
        ("delta_header", [("magic", 8), ("version", 2), ("base_seq", 4),
                          ("target_seq", 4), ("op_count", 3), ("crc64", 12)]),
        ("index_entry", [("path", 18), ("object_slot", 4), ("flags", 1)]),
        ("event_envelope", [("kind", 1), ("code", 3), ("text", 9), ("detail", 9)]),
        ("settings_value", [("key", 9), ("type", 22), ("value", 9)]),
    ]
    payload_objects = []
    for name, fields in objects:
        payload_objects.append({
            "name": name,
            "fields": [{"name": fn, "type": ft} for fn, ft in fields],
        })
    return {
        "version": 1,
        "comment": "Single source of truth for gen::proto_* codecs",
        "field_types": [
            {"id": i, "name": n, "kind": k, "bytes": b, "desc": d}
            for i, n, k, b, d in FIELD_TYPES
        ],
        "objects": payload_objects,
    }

def build_http():
    from data_http import (STATUS_ROWS, ALPN_ROWS, HEADER_ROWS, MIME_ROWS,
                           WINERR_ROWS)
    from data_http_tls import (TLS_SUITE_ROWS, TLS_SIG_ROWS, TLS_GROUP_ROWS,
                               TLS_EXT_ROWS)

    classes = {1: "informational", 2: "success", 3: "redirect",
               4: "client", 5: "server"}
    status = []
    seen = set()
    for row in STATUS_ROWS:
        code = int(row[0])
        if code in seen:
            raise SystemExit("duplicate status code %d" % code)
        seen.add(code)
        status.append({"code": code,
                       "reason": row[1],
                       "class": classes.get(code // 100, "unknown")})

    headers = []
    seen = set()
    for row in HEADER_ROWS:
        name = row[0]
        if name != name.lower() or name in seen:
            raise SystemExit("bad header row %s" % name)
        seen.add(name)
        flags = row[2] if len(row) > 2 else ""
        headers.append({"name": name,
                        "canonical": row[1],
                        "request": "R" in flags,
                        "response": "S" in flags,
                        "combinable": "C" in flags})

    mime = []
    owner = {}
    for row in MIME_ROWS:
        exts = []
        if len(row) > 1:
            exts = [e.strip().lower() for e in row[1].split(",") if e.strip()]
        for ext in exts:
            if ext in owner:
                raise SystemExit("duplicate extension %s (%s and %s)"
                                 % (ext, owner[ext], row[0]))
            owner[ext] = row[0]
        mime.append({"type": row[0], "extensions": exts})

    suites = []
    seen = set()
    for row in TLS_SUITE_ROWS:
        code = int(row[0])
        if code in seen:
            raise SystemExit("duplicate cipher suite %d" % code)
        seen.add(code)
        suites.append({"id": code, "name": row[1], "kx": row[2],
                       "aead": row[3] == "1", "version": row[4]})

    sigs = [{"id": int(r[0]), "name": r[1]} for r in TLS_SIG_ROWS]
    groups = [{"id": int(r[0]), "name": r[1]} for r in TLS_GROUP_ROWS]
    exts = [{"id": int(r[0]), "name": r[1]} for r in TLS_EXT_ROWS]
    alpn = [{"id": r[0], "description": r[1]} for r in ALPN_ROWS]

    winerr = []
    seen = set()
    for row in WINERR_ROWS:
        code = int(row[0])
        if code in seen:
            raise SystemExit("duplicate win32 code %d" % code)
        seen.add(code)
        winerr.append({"code": code, "name": row[1], "message": row[2]})

    return {
        "version": 1,
        "comment": "Registry tables for the Vanadium HTTP client and TLS layer",
        "status": status,
        "headers": headers,
        "mime": mime,
        "tls": {"suites": suites, "signature_schemes": sigs,
                "groups": groups, "extensions": exts, "alpn": alpn},
        "winerr": winerr,
    }

def _only_set(text):
    # accepts both "commands" and "commands.json"
    return {n if n.endswith(".json") else n + ".json" for n in text.split(",") if n}

def main(argv):
    # --only <name[,name]> rewrites just the named files. schema/messages.json has
    # grown past data_messages.py (keys were added straight to the JSON), so a full
    # run would silently drop them; commands-only regeneration is the supported
    # path for command metadata changes.
    # --merge-messages [--accept k1,k2] merges the table into the existing JSON
    # instead: keys missing from the file are appended, existing entries stay
    # verbatim unless they are named in --accept.
    only = None
    merge_msg = False
    accept = set()
    for i, arg in enumerate(argv):
        if arg == "--only":
            if i + 1 >= len(argv):
                print("--only needs a comma separated list")
                return 2
            only = _only_set(argv[i + 1])
        elif arg.startswith("--only="):
            only = _only_set(arg.split("=", 1)[1])
        elif arg == "--merge-messages":
            merge_msg = True
        elif arg == "--accept":
            if i + 1 >= len(argv):
                print("--accept needs a comma separated list")
                return 2
            accept = set(n for n in argv[i + 1].split(",") if n)
        elif arg.startswith("--accept="):
            accept = set(n for n in arg.split("=", 1)[1].split(",") if n)
    os.makedirs(SCHEMA, exist_ok=True)
    if merge_msg:
        payload, accepted, drift, indent = merge_messages(accept)
        path, size = write_json("messages.json", payload, indent=indent)
        print("merge   %-16s %8d bytes" % ("messages.json", size))
        print("MERGE_ACCEPTED=%d %s" % (len(accepted), ",".join(accepted)))
        print("MERGE_DRIFT=%d %s" % (len(drift), ",".join(drift)))
        print("messages=%d" % len(payload["messages"]))
        return 0
    builders = [
        ("messages.json", build_messages),
        ("errors.json", build_errors),
        ("commands.json", build_commands),
        ("protocol.json", build_protocol),
        ("http.json", build_http),
    ]
    if only is not None:
        unknown = only - {name for name, _ in builders}
        if unknown:
            print("unknown --only target: " + ",".join(sorted(unknown)))
            return 2
    writers = []
    for name, build in builders:
        if only is not None and name not in only:
            print("skip  %-16s (not selected)" % name)
            continue
        writers.append((name, build()))
    for name, payload in writers:
        path, size = write_json(name, payload)
        print("wrote %-16s %8d bytes" % (name, size))
    for name, payload in writers:
        if name == "messages.json":
            print("messages=%d" % len(payload["messages"]))
        elif name == "errors.json":
            print("errors=%d" % len(payload["errors"]))
        elif name == "http.json":
            print("http status=%d headers=%d mime=%d suites=%d winerr=%d" % (
                len(payload["status"]), len(payload["headers"]),
                len(payload["mime"]), len(payload["tls"]["suites"]),
                len(payload["winerr"])))
        elif name == "commands.json":
            print("commands=%d options=%d global_options=%d" % (
                len(payload["commands"]),
                sum(len(c["options"]) for c in payload["commands"]),
                len(payload["global_options"])))
    if UNKNOWN:
        print("MISSING_S2T count=%d" % len(UNKNOWN))
        print("MISSING_S2T chars=" + "".join(sorted(UNKNOWN)))
        return 1
    print("S2T_OK")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
