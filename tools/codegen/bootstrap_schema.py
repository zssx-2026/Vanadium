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
书書 仓倉 储儲 总總 碍礙 访訪 调調 远遠 镜鏡 静靜 页頁 额額

丢丟 两兩 产產 仅僅 体體 凑湊 双雙 壳殼 头頭 尽盡 帧幀 帮幫 异異 抛拋 护護 择擇 旧舊 暂暫 树樹 没沒 称稱 稳穩 紧緊 绑綁 绪緒 联聯 脚腳 补補 视視 译譯 账帳 踪蹤 车車 较較 辑輯 锁鎖 顺順 驱驅 当當

为為 这這 个個 们們 于於 与與 后後 会會 说說 时時 见見 让讓 对對 应應 该該
种種 样樣 现現 实實 际際 处處 图圖 标標 记記 忆憶 库庫 场場 划劃 统統 计計
边邊 态態 势勢 问問 题題 决決 断斷 据據 测測 试試 认認 证證 错錯 误誤 输輸
读讀 写寫 开開 关關 闭閉 删刪 贝貝 复復 贴貼 缓緩 间間 录錄 夹夾 盘盤 号號
编編 码碼 节節 长長 内內 网網 络絡 连連 响響 带帶 宽寬 进進 余餘 满滿 条條
许許 将將 从從 给給 规規 则則 义義 释釋 参參 构構 组組 验驗 钥鑰 随隨 机機
状狀 叠疊 换換 佔占 环環 变變 备備 尔爾 确確 强強 盖蓋 递遞 归歸 层層 浅淺
审審 诊診 况況 报報 页頁 访訪 试試 显顯 调調 节節 静靜 默認 认認 启啟 动動
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
一 不 串 久 也 了 交 他 以 任 但 何 便 停 允 元 六 其 准 初 加 助 包 化 十 印 即 卷 原 叉 及 受 只 可 台 同 吐 向 吞 含 因 圾 垃 基 外 多 套 如 子 字 孤 定 就 嵌 工 常 幂 平 形 彩 必 志 忘 忙 意 或 截 打 抑 拉 持 指 按 控 提 收 新 早 更 替 有 望 束 染 核 根 次 止 歧 段 每 比 渲 父 版 由 留 的 相 看 短 碰 禁 程 端 答 策 算 系 素 翻 而 色 落 裂 起 越 跟 追 集 零 非 首 魔

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
报 告 文 格 式 摘 要 镜 像 签 名 发 布 仓 库 分 支 节 点 快 照 增 量 回 滚 校 验 对
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

def all_langs(en, zh):
    return {"en": en, "zh-cn": zh, "tr-cn": to_traditional(zh), "am-en": to_american(en)}

def write_json(name, payload):
    path = os.path.join(SCHEMA, name)
    text = json.dumps(payload, ensure_ascii=False, indent=1, sort_keys=False) + "\n"
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    os.replace(tmp, path)
    return path, len(text)

def build_messages():
    seen = set()
    messages = {}
    for key, en, zh in MSG:
        if key in seen:
            raise SystemExit("duplicate message key: " + key)
        seen.add(key)
        messages[key] = all_langs(en, zh)
    return {
        "version": 1,
        "languages": LANGS,
        "default": "en",
        "comment": "Single source of truth for gen::msg_lookup; generated by tools/codegen/gen.py",
        "messages": messages,
    }

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

def main():
    os.makedirs(SCHEMA, exist_ok=True)
    writers = [
        ("messages.json", build_messages()),
        ("errors.json", build_errors()),
        ("commands.json", build_commands()),
        ("protocol.json", build_protocol()),
    ]
    for name, payload in writers:
        path, size = write_json(name, payload)
        print("wrote %-16s %8d bytes" % (name, size))
    print("messages=%d errors=%d commands=%d field_types=%d objects=%d" % (
        len(writers[0][1]["messages"]), len(writers[1][1]["errors"]),
        len(writers[2][1]["commands"]), len(writers[3][1]["field_types"]),
        len(writers[3][1]["objects"])))
    if UNKNOWN:
        print("MISSING_S2T count=%d" % len(UNKNOWN))
        print("MISSING_S2T chars=" + "".join(sorted(UNKNOWN)))
        return 1
    print("S2T_OK")
    return 0

if __name__ == "__main__":
    sys.exit(main())
