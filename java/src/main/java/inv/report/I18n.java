package inv.report;

import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Output catalogs. English is the default; zh-cn (simplified), tr-cn (traditional) and
 * am-en (US English) are selectable with --lang or the INV_LANG environment variable.
 * No user-facing string is hardcoded in the command classes.
 */
public final class I18n {

  public enum Lang {
    EN("en"),
    ZH_CN("zh-cn"),
    TR_CN("tr-cn"),
    AM_EN("am-en");

    public final String code;

    Lang(String code) {
      this.code = code;
    }

    public static Lang parse(String raw) {
      if (raw == null || raw.isBlank()) {
        return EN;
      }
      String s = raw.trim().toLowerCase(Locale.ROOT).replace('_', '-');
      return switch (s) {
        case "zh", "zh-cn", "zhcn", "zh-hans", "cn", "chinese", "simplified" -> ZH_CN;
        case "tr-cn", "trcn", "tr", "zh-tw", "zh-hant", "tw", "traditional", "trad" -> TR_CN;
        case "am-en", "amen", "am", "us", "en-us", "american" -> AM_EN;
        default -> EN;
      };
    }

    /** Strict variant for an explicit --lang value: null means "not a known catalog". */
    public static Lang strict(String raw) {
      if (raw == null) {
        return null;
      }
      String s = raw.trim().toLowerCase(Locale.ROOT).replace('_', '-');
      return switch (s) {
        case "en", "en-us", "english", "" -> EN;
        case "zh", "zh-cn", "zhcn", "zh-hans", "cn", "chinese", "simplified" -> ZH_CN;
        case "tr-cn", "trcn", "tr", "zh-tw", "zh-hant", "tw", "traditional", "trad" -> TR_CN;
        case "am-en", "amen", "am", "us", "american" -> AM_EN;
        default -> null;
      };
    }
  }

  private final Lang lang;
  private final Map<String, String> catalog;

  private I18n(Lang lang, Map<String, String> catalog) {
    this.lang = lang;
    this.catalog = catalog;
  }

  public static I18n forLang(Lang lang) {
    Map<String, String> merged = new LinkedHashMap<>(EN);
    switch (lang) {
      case ZH_CN -> merged.putAll(ZH_CN);
      case TR_CN -> merged.putAll(TR_CN);
      case AM_EN -> merged.putAll(AM_EN);
      default -> {
        // EN is already the base
      }
    }
    return new I18n(lang, merged);
  }

  public Lang lang() {
    return lang;
  }

  public String t(String key, Object... args) {
    String s = catalog.getOrDefault(key, EN.getOrDefault(key, key));
    for (int k = 0; k < args.length; k++) {
      s = s.replace("{" + k + "}", String.valueOf(args[k]));
    }
    return s;
  }

  private static Map<String, String> m(String... kv) {
    if (kv.length % 2 != 0) {
      throw new IllegalArgumentException("catalog entries must be key/value pairs");
    }
    Map<String, String> map = new LinkedHashMap<>();
    for (int i = 0; i < kv.length; i += 2) {
      map.put(kv[i], kv[i + 1]);
    }
    return map;
  }

  private static final Map<String, String> EN = m(
      "app.name", "vanadium-report",
      "usage.title", "Usage",
      "usage.synopsis", "  java -jar vanadium-report.jar <command> [options]",
      "usage.commands", "Commands",
      "usage.report", "  report    --archive <root> [--json]                  archive summary, branch tree, dedup rate",
      "usage.audit", "  audit     --archive <root> [--json] [--strict]      integrity check: objects, sha256, orphans",
      "usage.inspect", "  inspect   --archive <root> --branch <path> [--limit N] [--offset N] [--json]",
      "usage.version", "  version   [--json]                                  print version",
      "usage.options", "Options",
      "usage.help", "  -h, --help                                          show this help",
      "usage.lang", "  --lang <en|zh-cn|tr-cn|am-en>                       output language (default en, env INV_LANG)",
      "usage.pretty", "  --pretty                                            indent --json output (default: one line)",
      "usage.exit", "Exit codes: 0 ok, 1 error or audit failure.",
      "err.lang", "unknown language: {0} (expected en, zh-cn, tr-cn, am-en)",
      "err.usage", "unknown or missing command; run with --help",
      "err.option.value", "missing value for option {0}",
      "err.option.unknown", "unknown option: {0}",
      "err.arg.unknown", "unexpected argument: {0}",
      "err.archive.required", "--archive <root> is required",
      "err.archive.missing", "archive not found: {0}",
      "err.archive.meta", "archive metadata missing: {0}",
      "err.archive.json", "invalid JSON in {0}: {1}",
      "err.io", "cannot read {0}: {1}",
      "err.branch.required", "--branch <path> is required",
      "err.branch.missing", "branch not found: {0}",
      "err.manifest.missing", "manifest missing for branch {0}",
      "err.manifest.invalid", "manifest invalid for branch {0}: {1}",
      "err.limit", "{0} expects a positive integer",
      "err.internal", "internal error: {0}",
      "report.title", "Archive report",
      "report.root", "root",
      "report.meta", "metadata",
      "report.name", "name",
      "report.id", "id",
      "report.type", "type",
      "report.created", "created",
      "report.branches", "branches",
      "report.tree", "Branch tree",
      "report.stats", "Branch stats",
      "report.objects", "Object store",
      "report.col.branch", "BRANCH",
      "report.col.head", "HEAD",
      "report.col.files", "FILES",
      "report.col.dirs", "DIRS",
      "report.col.bytes", "BYTES",
      "report.col.manifest", "MANIFEST",
      "report.manifest.ok", "ok",
      "report.manifest.missing", "missing",
      "report.manifest.invalid", "invalid",
      "report.objects.count", "objects",
      "report.objects.bytes", "object bytes",
      "report.objects.referenced", "referenced",
      "report.objects.dedup", "dedup rate",
      "report.objects.dedup.note", "1 - {0} stored / {1} referenced",
      "report.objects.detail", "{0} objects, {1} references",
      "report.empty", "(no branches)",
      "report.warn", "warning: {0}",
      "audit.title", "Archive audit",
      "audit.checks", "Checks",
      "audit.issues", "Issues",
      "audit.result", "RESULT",
      "audit.pass", "PASS",
      "audit.fail", "FAIL",
      "audit.count.branches", "branches",
      "audit.count.manifests", "manifests",
      "audit.count.entries", "entries",
      "audit.count.files", "files",
      "audit.count.dirs", "dirs",
      "audit.count.objects", "objects",
      "audit.count.referenced", "referenced objects",
      "audit.count.missing", "missing objects",
      "audit.count.orphans", "orphan objects",
      "audit.count.badsha", "bad sha256",
      "audit.count.badpaths", "bad paths",
      "audit.count.duplicates", "duplicate entries",
      "audit.check.objects", "objects present",
      "audit.check.sha", "sha256 format",
      "audit.check.paths", "entry paths",
      "audit.check.manifests", "manifests readable",
      "audit.check.duplicates", "duplicate entries",
      "audit.check.orphans", "orphan objects",
      "audit.check.ok", "ok",
      "audit.check.fail", "{0} problem(s)",
      "audit.check.warn", "{0} warning(s)",
      "audit.issue", "{0}: {1}",
      "audit.clean", "no issues found",
      "audit.more", "... and {0} more",
      "audit.strict", "strict mode: orphan objects fail the audit",
      "inspect.title", "Snapshot",
      "inspect.branch", "branch",
      "inspect.seq", "seq",
      "inspect.id", "id",
      "inspect.time", "time",
      "inspect.message", "message",
      "inspect.parent", "parent",
      "inspect.entries", "entries",
      "inspect.col.mode", "MODE",
      "inspect.col.size", "SIZE",
      "inspect.col.sha", "SHA256",
      "inspect.col.path", "PATH",
      "inspect.page", "showing {0}-{1} of {2}",
      "inspect.empty", "(empty snapshot)",
      "inspect.dir", "dir",
      "version.line", "vanadium-report {0} (java {1}, {2})");

  private static final Map<String, String> ZH_CN = m(
      "usage.title", "用法",
      "usage.synopsis", "  java -jar vanadium-report.jar <命令> [选项]",
      "usage.commands", "命令",
      "usage.report", "  report    --archive <root> [--json]                 存档摘要、分支树、去重率",
      "usage.audit", "  audit     --archive <root> [--json] [--strict]     完整性校验：对象、sha256、孤儿对象",
      "usage.inspect", "  inspect   --archive <root> --branch <路径> [--limit N] [--offset N] [--json]",
      "usage.version", "  version   [--json]                                 输出版本",
      "usage.options", "选项",
      "usage.help", "  -h, --help                                         显示本帮助",
      "usage.lang", "  --lang <en|zh-cn|tr-cn|am-en>                      输出语言（默认 en，环境变量 INV_LANG）",
      "usage.pretty", "  --pretty                                            --json 输出缩进美化（默认单行）",
      "usage.exit", "退出码：0 成功，1 错误或审计失败。",
      "err.usage", "命令缺失或未知；使用 --help 查看帮助",
      "err.option.value", "选项 {0} 缺少取值",
      "err.option.unknown", "未知选项：{0}",
      "err.arg.unknown", "多余参数：{0}",
      "err.archive.required", "需要 --archive <root>",
      "err.archive.missing", "找不到存档：{0}",
      "err.archive.meta", "缺少存档元数据：{0}",
      "err.archive.json", "{0} 中的 JSON 无效：{1}",
      "err.io", "无法读取 {0}：{1}",
      "err.branch.required", "需要 --branch <路径>",
      "err.branch.missing", "找不到分支：{0}",
      "err.manifest.missing", "分支 {0} 的清单缺失",
      "err.manifest.invalid", "分支 {0} 的清单无效：{1}",
      "err.limit", "{0} 需要正整数",
      "err.internal", "内部错误：{0}",
      "report.title", "存档报告",
      "report.root", "根目录",
      "report.meta", "元数据",
      "report.name", "名称",
      "report.id", "标识",
      "report.type", "类型",
      "report.created", "创建时间",
      "report.branches", "分支",
      "report.tree", "分支树",
      "report.stats", "分支统计",
      "report.objects", "对象库",
      "report.col.branch", "分支",
      "report.col.head", "最新序号",
      "report.col.files", "文件",
      "report.col.dirs", "目录",
      "report.col.bytes", "字节",
      "report.col.manifest", "清单",
      "report.manifest.ok", "正常",
      "report.manifest.missing", "缺失",
      "report.manifest.invalid", "无效",
      "report.objects.count", "对象数",
      "report.objects.bytes", "对象字节",
      "report.objects.referenced", "引用",
      "report.objects.dedup", "去重率",
      "report.objects.dedup.note", "1 - 已存 {0} / 引用 {1}",
      "report.objects.detail", "{0} 个对象，{1} 次引用",
      "report.empty", "（无分支）",
      "report.warn", "警告：{0}",
      "audit.title", "存档审计",
      "audit.checks", "检查项",
      "audit.issues", "问题",
      "audit.result", "结果",
      "audit.count.branches", "分支",
      "audit.count.manifests", "清单",
      "audit.count.entries", "条目",
      "audit.count.files", "文件",
      "audit.count.dirs", "目录",
      "audit.count.objects", "对象",
      "audit.count.referenced", "被引用对象",
      "audit.count.missing", "缺失对象",
      "audit.count.orphans", "孤儿对象",
      "audit.count.badsha", "sha256 异常",
      "audit.count.badpaths", "路径异常",
      "audit.count.duplicates", "重复条目",
      "audit.check.objects", "对象存在",
      "audit.check.sha", "sha256 格式",
      "audit.check.paths", "条目路径",
      "audit.check.manifests", "清单可读",
      "audit.check.duplicates", "重复条目",
      "audit.check.orphans", "孤儿对象",
      "audit.check.ok", "通过",
      "audit.check.fail", "{0} 项问题",
      "audit.check.warn", "{0} 项警告",
      "audit.issue", "{0}：{1}",
      "audit.clean", "未发现问题",
      "audit.more", "……另有 {0} 项",
      "audit.strict", "严格模式：孤儿对象计为失败",
      "inspect.title", "快照",
      "inspect.branch", "分支",
      "inspect.seq", "序号",
      "inspect.id", "标识",
      "inspect.time", "时间",
      "inspect.message", "说明",
      "inspect.parent", "父快照",
      "inspect.entries", "条目",
      "inspect.col.mode", "权限",
      "inspect.col.size", "大小",
      "inspect.col.path", "路径",
      "inspect.page", "显示 {0}-{1} / 共 {2}",
      "inspect.empty", "（空快照）",
      "inspect.dir", "目录",
      "version.line", "vanadium-report {0}（Java {1}，{2}）");

  private static final Map<String, String> TR_CN = m(
      "usage.title", "用法",
      "usage.synopsis", "  java -jar vanadium-report.jar <指令> [選項]",
      "usage.commands", "指令",
      "usage.report", "  report    --archive <root> [--json]                 存檔摘要、分支樹、去重率",
      "usage.audit", "  audit     --archive <root> [--json] [--strict]     完整性校驗：物件、sha256、孤兒物件",
      "usage.inspect", "  inspect   --archive <root> --branch <路徑> [--limit N] [--offset N] [--json]",
      "usage.version", "  version   [--json]                                 輸出版本",
      "usage.options", "選項",
      "usage.help", "  -h, --help                                         顯示本說明",
      "usage.lang", "  --lang <en|zh-cn|tr-cn|am-en>                      輸出語言（預設 en，環境變數 INV_LANG）",
      "usage.pretty", "  --pretty                                            --json 輸出縮排美化（預設單行）",
      "usage.exit", "退出碼：0 成功，1 錯誤或審計失敗。",
      "err.usage", "指令缺失或未知；使用 --help 查看說明",
      "err.option.value", "選項 {0} 缺少取值",
      "err.option.unknown", "未知選項：{0}",
      "err.arg.unknown", "多餘參數：{0}",
      "err.archive.required", "需要 --archive <root>",
      "err.archive.missing", "找不到存檔：{0}",
      "err.archive.meta", "缺少存檔中繼資料：{0}",
      "err.archive.json", "{0} 中的 JSON 無效：{1}",
      "err.io", "無法讀取 {0}：{1}",
      "err.branch.required", "需要 --branch <路徑>",
      "err.branch.missing", "找不到分支：{0}",
      "err.manifest.missing", "分支 {0} 的清單遺失",
      "err.manifest.invalid", "分支 {0} 的清單無效：{1}",
      "err.limit", "{0} 需要正整數",
      "err.internal", "內部錯誤：{0}",
      "report.title", "存檔報告",
      "report.root", "根目錄",
      "report.meta", "中繼資料",
      "report.name", "名稱",
      "report.id", "識別碼",
      "report.type", "類型",
      "report.created", "建立時間",
      "report.branches", "分支",
      "report.tree", "分支樹",
      "report.stats", "分支統計",
      "report.objects", "物件庫",
      "report.col.branch", "分支",
      "report.col.head", "最新序號",
      "report.col.files", "檔案",
      "report.col.dirs", "目錄",
      "report.col.bytes", "位元組",
      "report.col.manifest", "清單",
      "report.manifest.ok", "正常",
      "report.manifest.missing", "遺失",
      "report.manifest.invalid", "無效",
      "report.objects.count", "物件數",
      "report.objects.bytes", "物件位元組",
      "report.objects.referenced", "參照",
      "report.objects.dedup", "去重率",
      "report.objects.dedup.note", "1 - 已存 {0} / 參照 {1}",
      "report.objects.detail", "{0} 個物件，{1} 次參照",
      "report.empty", "（無分支）",
      "report.warn", "警告：{0}",
      "audit.title", "存檔審計",
      "audit.checks", "檢查項",
      "audit.issues", "問題",
      "audit.result", "結果",
      "audit.count.branches", "分支",
      "audit.count.manifests", "清單",
      "audit.count.entries", "項目",
      "audit.count.files", "檔案",
      "audit.count.dirs", "目錄",
      "audit.count.objects", "物件",
      "audit.count.referenced", "被參照物件",
      "audit.count.missing", "遺失物件",
      "audit.count.orphans", "孤兒物件",
      "audit.count.badsha", "sha256 異常",
      "audit.count.badpaths", "路徑異常",
      "audit.count.duplicates", "重複項目",
      "audit.check.objects", "物件存在",
      "audit.check.sha", "sha256 格式",
      "audit.check.paths", "項目路徑",
      "audit.check.manifests", "清單可讀",
      "audit.check.duplicates", "重複項目",
      "audit.check.orphans", "孤兒物件",
      "audit.check.ok", "通過",
      "audit.check.fail", "{0} 項問題",
      "audit.check.warn", "{0} 項警告",
      "audit.issue", "{0}：{1}",
      "audit.clean", "未發現問題",
      "audit.more", "……另有 {0} 項",
      "audit.strict", "嚴格模式：孤兒物件計為失敗",
      "inspect.title", "快照",
      "inspect.branch", "分支",
      "inspect.seq", "序號",
      "inspect.id", "識別碼",
      "inspect.time", "時間",
      "inspect.message", "說明",
      "inspect.parent", "父快照",
      "inspect.entries", "項目",
      "inspect.col.mode", "權限",
      "inspect.col.size", "大小",
      "inspect.col.path", "路徑",
      "inspect.page", "顯示 {0}-{1} / 共 {2}",
      "inspect.empty", "（空快照）",
      "inspect.dir", "目錄",
      "version.line", "vanadium-report {0}（Java {1}，{2}）");

  /** US English: title-case section headers and US punctuation conventions. */
  private static final Map<String, String> AM_EN = m(
      "usage.title", "Usage",
      "report.title", "Archive Report",
      "report.tree", "Branch Tree",
      "report.stats", "Branch Stats",
      "report.objects", "Object Store",
      "audit.title", "Archive Audit",
      "audit.checks", "Checks",
      "audit.issues", "Issues",
      "report.manifest.ok", "OK",
      "audit.check.ok", "OK",
      "usage.exit", "Exit codes: 0 = ok, 1 = error or audit failure.");
}
