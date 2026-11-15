# inv / Vanadium 设计说明

> 本文件描述架构与数据格式，是内核与外壳之间的契约。所有命令输出文本以用户给的规范为准，
> 但语言可切换（默认 en）。

## 1. 总体结构

```
inv.exe (Rust shell)
  │  C ABI (vd_create / vd_run / vd_destroy)
  ▼
vanadium.dll (C++20 kernel)
  ├── core/        json, hash, lz4, fs, path, glob, unicode, i18n, settings, ui
  ├── store/       objects, manifests, snapshots, index, gc
  ├── archive/     branch tree, archive types (full/nodeonly/least), delta, rollback
  ├── remote/      winhttp, github api, auth, multithread download, release
  └── gen/         generated: unicode tables, i18n catalogs, protocol codecs, self-test vectors
  │
  └── spawns java.exe (bundled jlink runtime) for report / audit
```

外壳只做三件事：解析 argv、渲染事件（\r 动画、进度条）、处理 Ctrl+C。
所有业务逻辑、文案、错误码都在内核，外壳不含业务判断。

## 2. 事件协议

内核通过回调向外壳发送结构化事件，外壳决定怎么画：

| kind | 含义 | 字段 |
| --- | --- | --- |
| LINE | 永久输出一行 | a=text |
| STATUS | 可覆盖行（Connecting. / .. / ...） | a=key, b=text |
| PROGRESS | 进度条 | a=phase, i1=done, i2=total, i3=bytes, i4=rate |
| ERROR | 错误 | a=message |
| PROMPT | 询问 Y/N | a=question，回调返回 1=yes |
| SUMMARY | 命令结束摘要（JSON） | a=json |
| STATUS_CLEAR | 清掉当前 STATUS 行 | — |

## 3. 存档格式 v1

```
<root>/.inv/
  archive.json                      # 存档元数据 + 分支树
  objects/<aa>/<sha256>             # 内容寻址对象（可选 LZ4 压缩）
  branches/<bid>/manifest.json      # 该分支当前快照清单
  branches/<bid>/history/<seq>.json # 历史快照（entries 摘要 + parent）
  deltas/<bid>/<seq>.inc            # 增量文件
  index/<bid>.idx                   # 二进制索引（路径 -> 对象）
  tmp/  locks/
```

分支路径即标识：`/branches/node1/brancha`，节点 = 有子分支的分支。
`/releases/tag/<v>` 与 `/releases/rawdata/<v>` 为系统分支，`list` 默认不展开。

### 3.1 三种存档类型

* `full`：每次快照保存全部数据（对象去重），回滚最快、占用最大。
* `nodeonly`：节点起点保存完整数据，其后只存增量（`.inc`）与哈希（`.hash`）。
* `least`：仅根节点保存完整数据，其余全部增量，占用最小、回滚最慢。

回滚：full 直接展开对象；nodeonly 找最近节点后逐级 infer；least 从根逐级 infer。

### 3.2 完整性

每个对象记录 SHA256；每个清单条目额外记录 CRC64（ECMA-182 反射多项式）。
`inv verify` 全量校验，`clone`/`download` 默认校验，`-noverify` 关闭。

## 4. 远程协议

远程 = GitHub 仓库。分支打包为单个 `ivpack`（LZ4 分块 + 清单）后作为 release 资产上传，
tag 固定为 `inv-archive`；`inv release` 使用用户给的 `-tag` 创建普通发布。
下载使用 HTTP Range 多线程，合并后校验 SHA256 + CRC64。

## 5. 设置

`%APPDATA%\\inv\\settings.json`，类型：int / float / bool / string。
`inv set <key>=<value>` 或 `inv set <key> <value>`，支持 `url_<name>` 动态键。
语言：en、zh-cn、tr-cn（繁體）、am-en（美式英语）。

## 6. 构建与发布

* `tools/build.ps1`：codegen → C++ (cmake/ninja) → Rust → Java/jlink → NSIS。
* `tools/acceptance.ps1`：十步安装态验收，输出 PASS/FAIL 与日志。
* `tools/publish.ps1`：上传到 `zssx-2026/Infinity-Vanade` 的固定 release。
