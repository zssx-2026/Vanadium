# -*- coding: utf-8 -*-
"""Source table for schema/commands.json (English + Simplified Chinese).

Every command declared in include/vanadium/commands.h is listed here; the table
is authoritative for the help listing, so no command may be invented here.
Each row: name, args (untranslated usage tail), en/zh summary, own options.
Option rows: (flag, arg, en, zh) -- arg is "" for boolean flags.
"""

GLOBAL_OPTS = [
    ("-yes", "", "Assume yes for prompts", "对提示默认回答是"),
    ("-json", "", "Emit machine readable JSON", "输出机器可读 JSON"),
    ("-quiet", "", "Suppress progress output", "抑制进度输出"),
    ("-no-color", "", "Disable colored output", "禁用彩色输出"),
    ("-lang", "<code>", "Select interface language", "选择界面语言"),
    ("-verbose", "", "Print extra diagnostics", "打印额外诊断信息"),
]

CMD = [
    {
        "name": "help", "args": "[command]",
        "en": "Show help for a command or list every command",
        "zh": "显示命令帮助或列出全部命令",
        "opts": [
            ("-all", "", "List every command with a one line summary", "列出全部命令及一行摘要"),
            ("-keys", "", "List message keys used by the catalog", "列出目录中的消息键"),
        ],
    },
    {
        "name": "version", "args": "",
        "en": "Print kernel, shell and archive format versions",
        "zh": "打印内核、外壳与存档格式版本",
        "opts": [
            ("-full", "", "Include build commit and compiler details", "包含构建提交与编译器详情"),
            ("-format", "<fmt>", "Print a single field such as abi", "只打印单个字段，例如 abi"),
        ],
    },
    {
        "name": "set", "args": "<key>[=<value>] [value]",
        "en": "Read or write a persistent setting",
        "zh": "读取或写入持久化设置",
        "opts": [
            ("-unset", "", "Remove the setting and fall back to default", "删除设置并恢复默认"),
            ("-list", "", "List every setting with its current value", "列出全部设置及其当前值"),
            ("-type", "<type>", "Force the value type: int float bool string", "强制值类型：int float bool string"),
            ("-url", "<name>", "Set a dynamic remote url_<name> key", "设置动态远程键 url_<name>"),
        ],
    },
    {
        "name": "init", "args": "[path]",
        "en": "Initialize a new archive in the given directory",
        "zh": "在指定目录初始化新存档",
        "opts": [
            ("-type", "<type>", "Archive type: full nodeonly least", "存档类型：full nodeonly least"),
            ("-name", "<name>", "Archive display name", "存档显示名称"),
            ("-remote", "<url>", "Attach a remote GitHub repository", "绑定远程 GitHub 仓库"),
            ("-force", "", "Initialize even if files already exist", "即使已有文件也初始化"),
        ],
    },
    {
        "name": "status", "args": "[path]",
        "en": "Show working tree changes against the current snapshot",
        "zh": "显示工作区相对当前快照的改动",
        "opts": [
            ("-short", "", "One line per changed path", "每个改动路径一行"),
            ("-branch", "<name>", "Compare against another branch", "与另一分支比较"),
            ("-untracked", "", "Include untracked files", "包含未跟踪文件"),
        ],
    },
    {
        "name": "push", "args": "[message]",
        "en": "Record a snapshot of the working tree",
        "zh": "记录工作区快照",
        "opts": [
            ("-message", "<text>", "Snapshot message", "快照说明"),
            ("-branch", "<name>", "Target branch path", "目标分支路径"),
            ("-type", "<type>", "Override archive type for this snapshot", "覆盖本次快照的存档类型"),
            ("-node", "", "Mark this snapshot as a node start", "将本快照标记为节点起点"),
            ("-dry-run", "", "Compute the snapshot without writing", "只计算不写入"),
        ],
    },
    {
        "name": "pull", "args": "[branch]",
        "en": "Apply remote snapshots to the working tree",
        "zh": "将远程快照应用到工作区",
        "opts": [
            ("-branch", "<name>", "Branch to pull", "要拉取的分支"),
            ("-force", "", "Overwrite local changes", "覆盖本地改动"),
            ("-noverify", "", "Skip SHA256 and CRC64 checks", "跳过 SHA256 与 CRC64 校验"),
        ],
    },
    {
        "name": "log", "args": "[branch]",
        "en": "List snapshots newest first",
        "zh": "按从新到旧列出快照",
        "opts": [
            ("-limit", "<n>", "Maximum number of snapshots", "最大快照数量"),
            ("-oneline", "", "Compact one line format", "紧凑单行格式"),
            ("-branch", "<name>", "Branch to inspect", "要查看的分支"),
            ("-stat", "", "Include per snapshot change statistics", "包含每个快照的变更统计"),
        ],
    },
    {
        "name": "diff", "args": "[from] [to]",
        "en": "Compare two snapshots, branches or the working tree",
        "zh": "比较两个快照、分支或工作区",
        "opts": [
            ("-name-only", "", "List changed paths only", "只列出改动路径"),
            ("-stat", "", "Show change counts per path", "显示每个路径的改动计数"),
            ("-binary", "", "Include binary file markers", "包含二进制文件标记"),
            ("-path", "<glob>", "Restrict to paths matching a glob", "仅限匹配 glob 的路径"),
        ],
    },
    {
        "name": "reg", "args": "<path> [alias]",
        "en": "Register an archive so it can be used by alias",
        "zh": "注册存档以便通过别名使用",
        "opts": [
            ("-alias", "<name>", "Alias used by other commands", "其他命令使用的别名"),
            ("-url", "<url>", "Associate a remote url", "关联远程地址"),
            ("-force", "", "Replace an existing registration", "替换已有注册"),
        ],
    },
    {
        "name": "unreg", "args": "<alias>",
        "en": "Remove an archive registration",
        "zh": "移除存档注册",
        "opts": [
            ("-keep-files", "", "Leave the archive directory untouched", "不触碰存档目录"),
            ("-all", "", "Remove every registration", "移除全部注册"),
        ],
    },
    {
        "name": "list", "args": "[path]",
        "en": "List branches, nodes and registered archives",
        "zh": "列出分支、节点与已注册存档",
        "opts": [
            ("-tree", "", "Render the branch tree", "渲染分支树"),
            ("-releases", "", "Include release system branches", "包含发布系统分支"),
            ("-depth", "<n>", "Limit tree depth", "限制树深度"),
        ],
    },
    {
        "name": "search", "args": "<pattern>",
        "en": "Search tracked content for a pattern",
        "zh": "在已跟踪内容中搜索模式",
        "opts": [
            ("-path", "<glob>", "Restrict to paths matching a glob", "仅限匹配 glob 的路径"),
            ("-branch", "<name>", "Search a specific branch", "搜索指定分支"),
            ("-case", "", "Case sensitive matching", "区分大小写"),
            ("-regex", "", "Treat the pattern as a regular expression", "将模式视为正则表达式"),
            ("-limit", "<n>", "Stop after n matches", "达到 n 个匹配后停止"),
        ],
    },
    {
        "name": "rb", "args": "<snapshot>",
        "en": "Roll the working tree back to an earlier snapshot",
        "zh": "将工作区回滚到更早的快照",
        "opts": [
            ("-branch", "<name>", "Branch to roll back", "要回滚的分支"),
            ("-keep-local", "", "Preserve local modifications where possible", "尽量保留本地修改"),
            ("-type", "<type>", "Rollback strategy override", "回滚策略覆盖"),
            ("-dry-run", "", "Report the plan without writing", "只报告计划不写入"),
        ],
    },
    {
        "name": "rm", "args": "<path>",
        "en": "Remove tracked paths from the current snapshot",
        "zh": "从当前快照移除已跟踪路径",
        "opts": [
            ("-cached", "", "Remove from the snapshot but keep files", "从快照移除但保留文件"),
            ("-recursive", "", "Remove directories recursively", "递归移除目录"),
            ("-force", "", "Ignore local modifications", "忽略本地修改"),
        ],
    },
    {
        "name": "clone", "args": "<url> [path]",
        "en": "Clone a remote archive into a new directory",
        "zh": "将远程存档克隆到新目录",
        "opts": [
            ("-branch", "<name>", "Branch to check out", "要检出的分支"),
            ("-tag", "<tag>", "Release tag to fetch", "要获取的发布标签"),
            ("-depth", "<n>", "Shallow clone depth", "浅克隆深度"),
            ("-noverify", "", "Skip SHA256 and CRC64 checks", "跳过 SHA256 与 CRC64 校验"),
            ("-type", "<type>", "Archive type to materialize", "要落地的存档类型"),
        ],
    },
    {
        "name": "fork", "args": "<branch> <new-branch>",
        "en": "Create a child branch from an existing branch",
        "zh": "从现有分支创建子分支",
        "opts": [
            ("-at", "<snapshot>", "Fork at a specific snapshot", "在指定快照处分叉"),
            ("-node", "", "Make the fork point a node start", "将分叉点设为节点起点"),
            ("-description", "<text>", "Branch description", "分支说明"),
        ],
    },
    {
        "name": "login", "args": "",
        "en": "Authorize this machine against the remote provider",
        "zh": "对远程服务进行本机授权",
        "opts": [
            ("-token", "", "Read a token from stdin instead of device flow", "从标准输入读取令牌而非设备流程"),
            ("-status", "", "Show the current authorization state", "显示当前授权状态"),
            ("-logout", "", "Forget stored credentials", "忘记已保存凭据"),
        ],
    },
    {
        "name": "release", "args": "[tag]",
        "en": "Package the archive and publish a GitHub release",
        "zh": "打包存档并发布 GitHub 发布",
        "opts": [
            ("-tag", "<tag>", "Release tag to create", "要创建的发布标签"),
            ("-notes", "<text>", "Release notes", "发布说明"),
            ("-draft", "", "Create the release as a draft", "以草稿形式创建发布"),
            ("-prerelease", "", "Mark the release as a pre-release", "标记为预发布"),
            ("-asset", "<path>", "Upload an extra asset", "上传额外资源"),
        ],
    },
    {
        "name": "edit", "args": "<path>",
        "en": "Open or replace a tracked file through the archive",
        "zh": "通过存档打开或替换已跟踪文件",
        "opts": [
            ("-content", "<text>", "Write the given content instead of editing", "写入给定内容而非编辑"),
            ("-append", "", "Append instead of replacing", "追加而非替换"),
            ("-encoding", "<enc>", "Text encoding, default utf-8", "文本编码，默认 utf-8"),
        ],
    },
    {
        "name": "download", "args": "<asset> [path]",
        "en": "Download a release asset with parallel ranges",
        "zh": "使用并行范围下载发布资源",
        "opts": [
            ("-threads", "<n>", "Number of parallel connections", "并行连接数"),
            ("-out", "<path>", "Output file path", "输出文件路径"),
            ("-resume", "", "Resume a partial download", "续传部分下载"),
            ("-noverify", "", "Skip SHA256 and CRC64 checks", "跳过 SHA256 与 CRC64 校验"),
        ],
    },
    {
        "name": "verify", "args": "[path]",
        "en": "Verify objects, manifests and deltas of an archive",
        "zh": "校验存档的对象、清单与增量",
        "opts": [
            ("-deep", "", "Recompute every hash from disk", "从磁盘重新计算全部哈希"),
            ("-repair", "", "Re-fetch corrupt objects from the remote", "从远程重新获取损坏对象"),
            ("-branch", "<name>", "Verify a single branch", "只校验单个分支"),
            ("-summary", "", "Print a compact summary only", "仅打印紧凑摘要"),
        ],
    },
    {
        "name": "gc", "args": "",
        "en": "Delete unreferenced objects and stale temporaries",
        "zh": "删除未引用对象与过期临时文件",
        "opts": [
            ("-dry-run", "", "Report reclaimable space without deleting", "只报告可回收空间不删除"),
            ("-aggressive", "", "Also repack and recompress objects", "同时重新打包与压缩对象"),
            ("-keep", "<n>", "Keep n recent snapshots untouched", "保留最近 n 个快照不动"),
        ],
    },
    {
        "name": "doctor", "args": "",
        "en": "Diagnose environment, permissions and archive health",
        "zh": "诊断环境、权限与存档健康状况",
        "opts": [
            ("-fix", "", "Apply safe automatic repairs", "执行安全的自动修复"),
            ("-network", "", "Include remote connectivity checks", "包含远程连通性检查"),
            ("-report", "", "Write the diagnosis to a report file", "将诊断写入报告文件"),
        ],
    },
    {
        "name": "report", "args": "[branch]",
        "en": "Generate an archive report using the Java sidecar",
        "zh": "使用 Java 边车生成存档报告",
        "opts": [
            ("-format", "<fmt>", "Report format: text json html", "报告格式：text json html"),
            ("-out", "<path>", "Output file path", "输出文件路径"),
            ("-audit", "", "Include integrity audit sections", "包含完整性审计章节"),
            ("-branch", "<name>", "Branch to report on", "要报告的分支"),
        ],
    },
    {
        "name": "remote", "args": "[action]",
        "en": "Show or change the remote repository binding",
        "zh": "显示或修改远程仓库绑定",
        "opts": [
            ("-set", "<url>", "Set the remote url", "设置远程地址"),
            ("-remove", "", "Remove the remote binding", "移除远程绑定"),
            ("-test", "", "Test connectivity and permissions", "测试连通性与权限"),
        ],
    },
    {
        "name": "self-test", "args": "[filter]",
        "en": "Run the generated kernel self-test vectors",
        "zh": "运行生成的内核自测向量",
        "opts": [
            ("-filter", "<text>", "Only run cases whose name matches", "只运行名称匹配的用例"),
            ("-list", "", "List case names without running", "只列出用例名不执行"),
            ("-fail-fast", "", "Stop at the first failing case", "首个失败用例即停止"),
        ],
    },
    {
        "name": "bench", "args": "",
        "en": "Measure hashing, compression and IO throughput",
        "zh": "测量哈希、压缩与 IO 吞吐",
        "opts": [
            ("-seconds", "<n>", "Target duration per benchmark", "每个基准的目标时长"),
            ("-size", "<n>", "Working set size in MiB", "工作集大小（MiB）"),
        ],
    },
    {
        "name": "completion", "args": "<shell>",
        "en": "Print a shell completion script",
        "zh": "打印 shell 补全脚本",
        "opts": [
            ("-shell", "<name>", "Target shell: pwsh bash zsh fish", "目标 shell：pwsh bash zsh fish"),
            ("-out", "<path>", "Write the script to a file", "将脚本写入文件"),
        ],
    },
]
