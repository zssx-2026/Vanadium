package inv.report;

import java.io.PrintStream;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** "report" command: archive summary, branch tree, per-branch stats and object-store dedup. */
public final class Report {

  public record Row(
      Model.Branch branch,
      int files,
      int dirs,
      int entries,
      long bytes,
      int objects,
      String manifestStatus) {}

  public record Totals(
      int branches,
      long files,
      long dirs,
      long entries,
      long bytes,
      int referencedObjects,
      int objects,
      long objectBytes,
      double dedup) {}

  private Report() {}

  public static int run(ArchiveStore a, I18n t, PrintStream out, boolean json, int jsonIndent) {
    List<Row> rows = new ArrayList<>();
    Set<String> referenced = new LinkedHashSet<>();
    long totalFiles = 0;
    long totalDirs = 0;
    long totalEntries = 0;
    long totalBytes = 0;

    for (Model.Branch b : a.branches()) {
      ArchiveStore.Manifest m = a.manifest(b);
      int files = 0;
      int dirs = 0;
      int entries = 0;
      long bytes = 0;
      Set<String> local = new HashSet<>();
      if (m.snapshot() != null) {
        for (Model.Entry e : m.snapshot().entries()) {
          entries++;
          if (e.dir()) {
            dirs++;
          } else {
            files++;
            bytes += Math.max(0L, e.size());
            if (Util.isSha256(e.sha())) {
              String sha = e.sha().toLowerCase(Locale.ROOT);
              local.add(sha);
              referenced.add(sha);
            }
          }
        }
      }
      rows.add(new Row(b, files, dirs, entries, bytes, local.size(), m.status()));
      totalFiles += files;
      totalDirs += dirs;
      totalEntries += entries;
      totalBytes += bytes;
    }

    ArchiveStore.ObjectIndex index = a.objects();
    double dedup = totalBytes > 0 ? 1.0 - (double) index.totalBytes() / (double) totalBytes : Double.NaN;
    Totals totals = new Totals(
        a.branches().size(),
        totalFiles,
        totalDirs,
        totalEntries,
        totalBytes,
        referenced.size(),
        index.sizes().size(),
        index.totalBytes(),
        dedup);

    if (json) {
      out.println(toJson(a, t, rows, totals).dump(jsonIndent));
    } else {
      printText(a, t, out, rows, totals);
    }
    return 0;
  }

  private static void printText(
      ArchiveStore a, I18n t, PrintStream out, List<Row> rows, Totals totals) {
    out.println(t.t("report.title"));
    field(out, t, "report.root", a.root.toString());
    field(out, t, "report.meta", a.inv.toString());
    field(out, t, "report.name", a.name);
    field(out, t, "report.id", a.id.isEmpty() ? "-" : a.id);
    field(out, t, "report.type", a.type);
    field(out, t, "report.created", Util.isoTime(a.createdMs));
    field(out, t, "report.branches", String.valueOf(a.branches().size()));

    out.println();
    out.println(t.t("report.tree"));
    if (a.branches().isEmpty()) {
      out.println("  " + t.t("report.empty"));
    } else {
      renderTree(a.branches(), out);
    }

    out.println();
    out.println(t.t("report.stats"));
    int wBranch = t.t("report.col.branch").length();
    for (Row r : rows) {
      wBranch = Math.max(wBranch, r.branch().path().length());
    }
    wBranch += 2;
    out.println("  " + Util.padRight(t.t("report.col.branch"), wBranch)
        + Util.padLeft(t.t("report.col.head"), 6)
        + Util.padLeft(t.t("report.col.files"), 7)
        + Util.padLeft(t.t("report.col.dirs"), 6)
        + Util.padLeft(t.t("report.col.bytes"), 12)
        + "  " + t.t("report.col.manifest"));
    for (Row r : rows) {
      out.println("  " + Util.padRight(r.branch().path(), wBranch)
          + Util.padLeft(String.valueOf(r.branch().headSeq()), 6)
          + Util.padLeft(String.valueOf(r.files()), 7)
          + Util.padLeft(String.valueOf(r.dirs()), 6)
          + Util.padLeft(String.valueOf(r.bytes()), 12)
          + "  " + t.t(manifestKey(r.manifestStatus())));
    }

    out.println();
    out.println(t.t("report.objects"));
    field(out, t, "report.objects.count", String.valueOf(totals.objects()));
    field(out, t, "report.objects.bytes",
        Util.humanSize(totals.objectBytes()) + " (" + totals.objectBytes() + " B)");
    field(out, t, "report.objects.referenced",
        t.t("report.objects.detail", totals.referencedObjects(), totals.files()));
    field(out, t, "report.objects.dedup",
        Util.percent(totals.dedup()) + "  ("
            + t.t("report.objects.dedup.note",
                Util.humanSize(totals.objectBytes()), Util.humanSize(totals.bytes()))
            + ")");

    for (String w : a.warnings) {
      out.println(w);
    }
  }

  private static String manifestKey(String status) {
    return switch (status) {
      case "missing" -> "report.manifest.missing";
      case "invalid" -> "report.manifest.invalid";
      default -> "report.manifest.ok";
    };
  }

  private static void field(PrintStream out, I18n t, String key, String value) {
    out.println("  " + Util.padRight(t.t(key), 12) + ": " + value);
  }

  /** ASCII branch tree (portable across Windows console code pages). */
  private static void renderTree(List<Model.Branch> branches, PrintStream out) {
    Map<String, Model.Branch> byPath = new TreeMap<>();
    for (Model.Branch b : branches) {
      byPath.put(b.path(), b);
    }
    Map<String, List<String>> children = new TreeMap<>();
    List<String> roots = new ArrayList<>();
    for (Model.Branch b : branches) {
      String parent = b.parent();
      if (parent == null || parent.isEmpty() || parent.equals("/") || !byPath.containsKey(parent)) {
        roots.add(b.path());
      } else {
        children.computeIfAbsent(parent, k -> new ArrayList<>()).add(b.path());
      }
    }
    roots.sort(String::compareTo);
    for (List<String> kids : children.values()) {
      kids.sort(String::compareTo);
    }
    Set<String> visited = new HashSet<>();
    for (String r : roots) {
      renderNode(r, "", true, children, visited, out);
    }
  }

  private static void renderNode(
      String path,
      String prefix,
      boolean last,
      Map<String, List<String>> children,
      Set<String> visited,
      PrintStream out) {
    if (!visited.add(path)) {
      return;
    }
    out.println(prefix + (prefix.isEmpty() ? "" : (last ? "+-- " : "|-- ")) + path);
    String childPrefix = prefix + (last ? "    " : "|   ");
    List<String> kids = children.getOrDefault(path, List.of());
    for (int i = 0; i < kids.size(); i++) {
      renderNode(kids.get(i), childPrefix, i == kids.size() - 1, children, visited, out);
    }
  }

  private static Json toJson(
      ArchiveStore a, I18n t, List<Row> rows, Totals totals) {
    Json root = Json.object();
    root.put("tool", t.t("app.name"));
    root.put("version", Cli.VERSION);
    root.put("command", "report");

    Json archive = Json.object();
    archive.put("root", a.root.toString());
    archive.put("metadata", a.inv.toString());
    archive.put("name", a.name);
    archive.put("id", a.id);
    archive.put("type", a.type);
    archive.put("created_ms", a.createdMs);
    archive.put("created", Util.isoTime(a.createdMs));
    root.put("archive", archive);

    Json branches = Json.array();
    for (Row r : rows) {
      Json b = Json.object();
      b.put("path", r.branch().path());
      b.put("id", r.branch().id());
      b.put("name", r.branch().name());
      b.put("parent", r.branch().parent());
      b.put("head_seq", r.branch().headSeq());
      b.put("is_node", r.branch().isNode());
      b.put("system", r.branch().system());
      b.put("files", r.files());
      b.put("dirs", r.dirs());
      b.put("entries", r.entries());
      b.put("bytes", r.bytes());
      b.put("objects", r.objects());
      b.put("manifest", r.manifestStatus());
      branches.add(b);
    }
    root.put("branches", branches);

    Json totalsJson = Json.object();
    totalsJson.put("branches", totals.branches());
    totalsJson.put("files", totals.files());
    totalsJson.put("dirs", totals.dirs());
    totalsJson.put("entries", totals.entries());
    totalsJson.put("bytes", totals.bytes());
    totalsJson.put("referenced_objects", totals.referencedObjects());
    totalsJson.put("objects", totals.objects());
    totalsJson.put("object_bytes", totals.objectBytes());
    if (Double.isNaN(totals.dedup())) {
      totalsJson.put("dedup_ratio", Json.ofNull());
    } else {
      totalsJson.put("dedup_ratio", totals.dedup());
    }
    root.put("totals", totalsJson);
    root.put("tree", treeJson(a.branches()));

    Json warnings = Json.array();
    for (String w : a.warnings) {
      warnings.add(Json.of(w));
    }
    root.put("warnings", warnings);
    return root;
  }

  private static Json treeJson(List<Model.Branch> branches) {
    Map<String, Model.Branch> byPath = new TreeMap<>();
    for (Model.Branch b : branches) {
      byPath.put(b.path(), b);
    }
    Map<String, List<String>> children = new TreeMap<>();
    List<String> roots = new ArrayList<>();
    for (Model.Branch b : branches) {
      String parent = b.parent();
      if (parent == null || parent.isEmpty() || parent.equals("/") || !byPath.containsKey(parent)) {
        roots.add(b.path());
      } else {
        children.computeIfAbsent(parent, k -> new ArrayList<>()).add(b.path());
      }
    }
    roots.sort(String::compareTo);
    Json arr = Json.array();
    Set<String> visited = new HashSet<>();
    for (String r : roots) {
      arr.add(nodeJson(r, children, visited));
    }
    return arr;
  }

  private static Json nodeJson(String path, Map<String, List<String>> children, Set<String> visited) {
    Json node = Json.object();
    node.put("path", path);
    Json kids = Json.array();
    if (visited.add(path)) {
      List<String> list = new ArrayList<>(children.getOrDefault(path, List.of()));
      list.sort(String::compareTo);
      for (String k : list) {
        kids.add(nodeJson(k, children, visited));
      }
    }
    node.put("children", kids);
    return node;
  }
}
