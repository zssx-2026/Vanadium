# -*- coding: utf-8 -*-
"""Source table for schema/commands.json (English + Simplified Chinese).

Every command declared in include/vanadium/commands.h is listed here; the table
is authoritative for the help listing, so no command may be invented here.
Each row: name, args (untranslated usage tail), en/zh summary, own options.
Option rows: (flag, arg, en, zh) -- arg is "" for boolean flags.

The table mirrors the 0.4.0 command surface in src/commands/*.cpp: a flag is
listed only when that command actually reads it (has_flag / opt_value), so
`inv help <cmd>` never advertises behaviour the binary does not have.
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
        "opts": [],
    },
    {
        "name": "version", "args": "",
        "en": "Print shell, kernel and archive format versions",
        "zh": "打印外壳、内核与存档格式版本",
        "opts": [],
    },
    {
        "name": "set", "args": "[key] [value]",
        "en": "Read or write a persistent setting",
        "zh": "读取或写入持久化设置",
        "opts": [
            ("-list", "", "List every setting with its current value", "列出全部设置及其当前值"),
            ("-unset", "<key>", "Remove the setting and fall back to the default", "删除设置并恢复默认值"),
        ],
    },
    {
        "name": "init", "args": "[path]",
        "en": "Initialize a new archive in the given directory",
        "zh": "在指定目录初始化新存档",
        "opts": [],
    },
    {
        "name": "status", "args": "[path]",
        "en": "Show working tree changes against the current snapshot",
        "zh": "显示工作树相对当前快照的变更",
        "opts": [
            ("-short", "", "Print one changed path per line", "每行只打印一个变更路径"),
        ],
    },
    {
        "name": "push", "args": "<url>",
        "en": "Upload the archive pack to a GitHub release",
        "zh": "把存档包上传到 GitHub release",
        "opts": [],
    },
    {
        "name": "pull", "args": "<url>",
        "en": "Download the archive pack from a GitHub release",
        "zh": "从 GitHub release 下载存档包",
        "opts": [
            ("-tag", "<tag>", "Release tag to fetch", "要下载的 release 标签"),
            ("-threads", "<n>", "Parallel connections, 1 to 16 (alias -thread)", "并行连接数，1 到 16（别名 -thread）"),
        ],
    },
    {
        "name": "log", "args": "",
        "en": "List snapshots from newest to oldest",
        "zh": "从新到旧列出快照",
        "opts": [
            ("-branch", "<name>", "Branch to read, defaults to the current one", "读取的分支，默认为当前分支"),
            ("-n", "<N>", "Maximum snapshots to print (alias -limit)", "最多打印的快照数（别名 -limit）"),
            ("-oneline", "", "One line per snapshot", "每个快照一行"),
            ("-stat", "", "Include changed file counts", "包含变更文件统计"),
        ],
    },
    {
        "name": "diff", "args": "",
        "en": "Compare the working tree with the current snapshot",
        "zh": "比较工作树与当前快照",
        "opts": [
            ("-name-only", "", "Print changed paths only", "只打印变更路径"),
            ("-stat", "", "Print per file change counts", "打印每个文件的变更统计"),
            ("-path", "<glob>", "Limit the comparison to matching paths", "只比较匹配的路径"),
        ],
    },
    {
        "name": "reg", "args": "<path>",
        "en": "Register a directory as a branch (requires an initialised workspace; writes .inv/<id>.inv)",
        "zh": "把目录注册为分支（要求工作区已初始化；写入 .inv/<id>.inv）",
        "opts": [
            ("-branch", "<name>", "Branch name, defaults to the directory name", "分支名，默认为目录名"),
            ("-type", "<type>", "Archive type: full nodeonly least", "存档类型：full nodeonly least"),
            ("-message", "<text>", "Snapshot message", "快照说明"),
        ],
    },
    {
        "name": "unreg", "args": "<path>",
        "en": "Remove a branch registration and its .inv record",
        "zh": "移除分支注册及其 .inv 记录",
        "opts": [
            ("-recursive", "", "Also remove registered children", "同时移除已注册的子分支"),
        ],
    },
    {
        "name": "list", "args": "[path]",
        "en": "List branches, nodes or the files of one branch",
        "zh": "列出分支、节点或某个分支的文件",
        "opts": [
            ("-tree", "", "Render the branch tree", "以树形显示分支"),
            ("-releases", "", "Also list the release system branches", "同时列出 release 系统分支"),
            ("-depth", "<n>", "Limit tree depth", "限制树深度"),
            ("-line", "", "One node per line", "每个节点一行"),
            ("-sys", "", "Include system nodes", "包含系统节点"),
        ],
    },
    {
        "name": "search", "args": "<pattern> [branch]",
        "en": "Search tracked snapshot content for a pattern",
        "zh": "在已跟踪快照内容中搜索模式",
        "opts": [
            ("-path", "<glob>", "Limit the search to matching paths", "只搜索匹配的路径"),
            ("-branch", "<name>", "Branch to search", "要搜索的分支"),
            ("-case", "", "Case sensitive match", "区分大小写"),
            ("-regex", "", "Treat the pattern as a regular expression", "把模式当作正则表达式"),
            ("-limit", "<n>", "Maximum matches to print", "最多打印的匹配数"),
            ("-name", "", "Match path names only, without reading content", "只匹配路径名，不读取文件内容"),
        ],
    },
    {
        "name": "rb", "args": "<path> <seq>",
        "en": "Roll the working tree back to an earlier snapshot",
        "zh": "把工作树回滚到更早的快照",
        "opts": [
            ("-path", "<dir>", "Materialize into this directory", "物化到该目录"),
            ("-yes", "", "Skip the confirmation prompt", "跳过确认提示"),
        ],
    },
    {
        "name": "rm", "args": "<branch>",
        "en": "Delete a branch from the archive",
        "zh": "从存档删除分支",
        "opts": [
            ("-recursive", "", "Also delete child branches", "同时删除子分支"),
            ("-yes", "", "Skip the confirmation prompt", "跳过确认提示"),
        ],
    },
    {
        "name": "clone", "args": "<url>",
        "en": "Clone a remote archive into a new directory",
        "zh": "把远程存档克隆到新目录",
        "opts": [
            ("-path", "<dir>", "Target directory, defaults to the repository name", "目标目录，默认为仓库名"),
            ("-threads", "<n>", "Parallel connections, 1 to 16 (alias -thread)", "并行连接数，1 到 16（别名 -thread）"),
            ("-type", "<type>", "Archive type: full nodeonly least", "存档类型：full nodeonly least"),
            ("-full", "", "Shortcut for -type full", "-type full 的简写"),
            ("-least", "", "Shortcut for -type least", "-type least 的简写"),
            ("-nodeonly", "", "Shortcut for -type nodeonly", "-type nodeonly 的简写"),
        ],
    },
    {
        "name": "fork", "args": "<url>",
        "en": "Clone a remote archive under a new name",
        "zh": "以新名称克隆远程存档",
        "opts": [
            ("-name", "<name>", "New archive name, defaults to the repository name", "新存档名，默认为仓库名"),
            ("-path", "<dir>", "Target directory, defaults to the new name", "目标目录，默认为新名称"),
        ],
    },
    {
        "name": "login", "args": "[url]",
        "en": "Store a GitHub token for this machine",
        "zh": "在本机保存 GitHub 令牌",
        "opts": [
            ("-token", "<token>", "Token to store", "要保存的令牌"),
            ("-name", "<user>", "Account name to store the token under", "保存令牌所用的账号名"),
        ],
    },
    {
        "name": "release", "args": "<url>",
        "en": "Create or list GitHub releases for the archive",
        "zh": "为存档创建或列出 GitHub release",
        "opts": [
            ("-tag", "<tag>", "Release tag, defaults to v1.0", "release 标记，默认为 v1.0"),
            ("-title", "<title>", "Release title, defaults to the tag", "release 标题，默认为标记"),
            ("-branch", "<name>", "Branch whose head is described", "用于描述的分支头"),
            ("-relist", "", "List existing releases instead of creating one", "改为列出已有 release"),
            ("-reintro", "", "Update the latest release instead of creating one", "改为更新最新 release"),
        ],
    },
    {
        "name": "edit", "args": "<type> <url>",
        "en": "Edit a url_<name> setting or a stored token",
        "zh": "编辑 url_<name> 设置或已保存的令牌",
        "opts": [
            ("-name", "<name>", "Setting name, required for type url", "设置名，type=url 时必填"),
            ("-token", "<token>", "Token value, used for type token", "令牌值，type=token 时使用"),
        ],
    },
    {
        "name": "download", "args": "<url>",
        "en": "Download a URL with parallel ranged requests",
        "zh": "用并行分段请求下载 URL",
        "opts": [
            ("-out", "<path>", "Output file path (alias -path)", "输出文件路径（别名 -path）"),
            ("-threads", "<n>", "Parallel connections, 1 to 16 (alias -thread)", "并行连接数，1 到 16（别名 -thread）"),
            ("-sha256", "<hex>", "Expected SHA-256 digest", "期望的 SHA-256 摘要"),
            ("-crc64", "<hex>", "Expected CRC-64 digest", "期望的 CRC-64 摘要"),
            ("-noverify", "", "Skip digest verification", "跳过摘要校验"),
        ],
    },
    {
        "name": "verify", "args": "",
        "en": "Verify every object hash in the archive",
        "zh": "校验存档中每个对象的哈希",
        "opts": [
            ("-summary", "", "Print a single summary line", "只打印一行汇总"),
        ],
    },
    {
        "name": "gc", "args": "",
        "en": "Delete objects that no snapshot references",
        "zh": "删除没有快照引用的对象",
        "opts": [
            ("-dry-run", "", "Report reclaimable space without deleting", "只报告可回收空间，不删除"),
        ],
    },
    {
        "name": "doctor", "args": "",
        "en": "Diagnose environment, settings and archive health",
        "zh": "诊断环境、设置与存档健康状态",
        "opts": [],
    },
    {
        "name": "report", "args": "",
        "en": "Write an archive report as JSON",
        "zh": "以 JSON 写出存档报告",
        "opts": [
            ("-out", "<path>", "Report path, defaults to inv-report.json in the current directory", "报告路径，默认为当前目录的 inv-report.json"),
        ],
    },
    {
        "name": "remote", "args": "",
        "en": "List the url_* remote bindings in settings",
        "zh": "列出设置中的 url_* 远程绑定",
        "opts": [],
    },
    {
        "name": "self-test", "args": "[filter]",
        "en": "Run the generated kernel self-test vectors",
        "zh": "运行生成的内核自检向量",
        "opts": [],
    },
    {
        "name": "bench", "args": "",
        "en": "Measure hashing, compression and IO throughput",
        "zh": "测量哈希、压缩与 IO 吞吐",
        "opts": [],
    },
    {
        "name": "completion", "args": "",
        "en": "Print a shell completion script",
        "zh": "打印 shell 补全脚本",
        "opts": [
            ("-powershell", "", "Emit PowerShell completion", "输出 PowerShell 补全"),
        ],
    },
]
