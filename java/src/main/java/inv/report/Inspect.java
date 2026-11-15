package inv.report;

import java.io.PrintStream;
import java.util.ArrayList;
import java.util.List;

/** "inspect" command: latest snapshot of one branch, with --limit/--offset paging. */
public final class Inspect {

  private Inspect() {}

  public static int run(
      ArchiveStore a, I18n t, PrintStream out, String branchArg, int limit, int offset, boolean json,
    int jsonIndent) {
    String path = Util.normalizeBranchPath(branchArg);
    Model.Branch b = a.find(path);
    if (b == null) {
      throw new AppException(t.t("err.branch.missing", path));
    }
    ArchiveStore.Manifest m = a.manifest(b);
    if ("missing".equals(m.status())) {
      throw new AppException(t.t("err.manifest.missing", b.path()));
    }
    if ("invalid".equals(m.status())) {
      throw new AppException(t.t("err.manifest.invalid", b.path(), m.error()));
    }
    Model.Snapshot s = m.snapshot();
    List<Model.Entry> entries = s.entries();
    int total = entries.size();
    int from = Math.min(Math.max(offset, 0), total);
    int to = Math.min(from + Math.max(limit, 1), total);
    List<Model.Entry> page = new ArrayList<>(entries.subList(from, to));

    if (json) {
      out.println(toJson(t, b, s, page, from, to, total, limit).dump(jsonIndent));
      return 0;
    }

    out.println(t.t("inspect.title"));
    field(out, t, "inspect.branch", b.path());
    field(out, t, "inspect.seq", String.valueOf(s.seq()));
    field(out, t, "inspect.id", s.id().isEmpty() ? "-" : s.id());
    field(out, t, "inspect.time", Util.isoTime(s.timeMs()));
    field(out, t, "inspect.message", s.message());
    field(out, t, "inspect.parent", s.parentSha().isEmpty() ? "-" : s.parentSha());
    field(out, t, "inspect.entries", String.valueOf(total));
    out.println();

    if (total == 0) {
      out.println("  " + t.t("inspect.empty"));
      return 0;
    }

    int wMode = Math.max(t.t("inspect.col.mode").length(), 6);
    int wSize = Math.max(t.t("inspect.col.size").length(), 9);
    out.println("  " + Util.padRight(t.t("inspect.col.mode"), wMode)
        + "  " + Util.padRight(t.t("inspect.col.size"), wSize)
        + "  " + Util.padRight(t.t("inspect.col.sha"), 16)
        + "  " + t.t("inspect.col.path"));
    for (Model.Entry e : page) {
      String mode = String.format(java.util.Locale.ROOT, "%06o", e.mode());
      String size = e.dir() ? "-" : Util.humanSize(e.size());
      String sha = e.dir() ? t.t("inspect.dir") : shortSha(e.sha());
      out.println("  " + Util.padRight(mode, wMode)
          + "  " + Util.padLeft(size, wSize)
          + "  " + Util.padRight(sha, 16)
          + "  " + e.path());
    }
    out.println();
    out.println("  " + t.t("inspect.page", from + 1, to, total));
    return 0;
  }

  private static String shortSha(String sha) {
    if (sha == null || sha.isEmpty()) {
      return "-";
    }
    return sha.length() <= 16 ? sha : sha.substring(0, 16);
  }

  private static void field(PrintStream out, I18n t, String key, String value) {
    out.println("  " + Util.padRight(t.t(key), 12) + ": " + value);
  }

  private static Json toJson(
      I18n t,
      Model.Branch b,
      Model.Snapshot s,
      List<Model.Entry> page,
      int from,
      int to,
      int total,
      int limit) {
    Json root = Json.object();
    root.put("tool", t.t("app.name"));
    root.put("version", Cli.VERSION);
    root.put("command", "inspect");

    Json branch = Json.object();
    branch.put("path", b.path());
    branch.put("id", b.id());
    branch.put("name", b.name());
    root.put("branch", branch);

    Json snap = Json.object();
    snap.put("seq", s.seq());
    snap.put("id", s.id());
    snap.put("time_ms", s.timeMs());
    snap.put("time", Util.isoTime(s.timeMs()));
    snap.put("message", s.message());
    snap.put("parent_sha", s.parentSha());
    snap.put("entries", total);
    root.put("snapshot", snap);

    Json entries = Json.array();
    for (Model.Entry e : page) {
      Json item = Json.object();
      item.put("path", e.path());
      item.put("dir", e.dir());
      item.put("size", e.size());
      item.put("sha", e.sha());
      item.put("mode", String.format(java.util.Locale.ROOT, "%06o", e.mode()));
      item.put("mtime", e.mtime());
      entries.add(item);
    }
    root.put("entries", entries);

    Json pageJson = Json.object();
    pageJson.put("offset", from);
    pageJson.put("limit", limit);
    pageJson.put("from", from);
    pageJson.put("to", to);
    pageJson.put("total", total);
    root.put("page", pageJson);
    return root;
  }
}
