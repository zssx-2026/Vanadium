package inv.report;

import java.io.PrintStream;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * "audit" command: verifies object existence, sha256 shape, entry paths, manifest
 * readability, duplicate entries and orphan objects.
 *
 * <p>PASS means zero fatal problems. Orphan objects are warnings by default; --strict makes
 * them fatal. Exit code is 0 for PASS and 1 for FAIL.
 */
public final class Audit {

  public record Issue(String check, String subject, String detail) {}

  public record Check(String name, String status, int problems) {}

  public record Result(
      boolean pass,
      boolean strict,
      long branches,
      long manifests,
      long entries,
      long files,
      long dirs,
      int objects,
      long objectBytes,
      int referenced,
      int missing,
      int orphans,
      int badSha,
      int badPaths,
      int duplicates,
      List<Check> checks,
      List<Issue> issues,
      List<String> warnings) {}

  private static final int MAX_ISSUES_PRINTED = 20;

  private Audit() {}

  public static Result check(ArchiveStore a, I18n t, boolean strict) {
    Map<String, Integer> problems = new LinkedHashMap<>();
    for (String k : new String[] {"manifests", "objects", "sha", "paths", "duplicates", "orphans"}) {
      problems.put(k, 0);
    }
    List<Issue> issues = new ArrayList<>();
    List<String> warnings = new ArrayList<>(a.warnings);

    Set<String> referenced = new LinkedHashSet<>();
    long entries = 0;
    long files = 0;
    long dirs = 0;
    int badSha = 0;
    int badPaths = 0;
    int duplicates = 0;
    long manifestsOk = 0;

    for (Model.Branch b : a.branches()) {
      ArchiveStore.Manifest m = a.manifest(b);
      if ("missing".equals(m.status())) {
        problems.merge("manifests", 1, Integer::sum);
        issues.add(new Issue("manifests", b.path(), "manifest.json missing: " + m.error()));
      } else if ("invalid".equals(m.status())) {
        problems.merge("manifests", 1, Integer::sum);
        issues.add(new Issue("manifests", b.path(), m.error()));
      } else {
        manifestsOk++;
      }
      if (m.snapshot() == null) {
        continue;
      }
      Set<String> seen = new LinkedHashSet<>();
      for (Model.Entry e : m.snapshot().entries()) {
        entries++;
        if (e.dir()) {
          dirs++;
        } else {
          files++;
          if (!Util.isSha256(e.sha())) {
            badSha++;
            problems.merge("sha", 1, Integer::sum);
            issues.add(new Issue("sha", b.path(), "entry '" + e.path() + "' sha256 '" + e.sha() + "'"));
          } else {
            referenced.add(e.sha().toLowerCase(Locale.ROOT));
          }
        }
        if (!Util.validEntryPath(e.path())) {
          badPaths++;
          problems.merge("paths", 1, Integer::sum);
          issues.add(new Issue("paths", b.path(), "entry path '" + e.path() + "'"));
        }
        if (!seen.add(e.path())) {
          duplicates++;
          problems.merge("duplicates", 1, Integer::sum);
          issues.add(new Issue("duplicates", b.path(), "duplicate entry '" + e.path() + "'"));
        }
      }
    }

    for (Model.Branch b : a.branches()) {
      ArchiveStore.HistoryLoad h = a.history(b);
      for (String err : h.errors()) {
        warnings.add(t.t("report.warn", "history " + b.path() + ": " + err));
      }
      for (Model.Snapshot s : h.snapshots()) {
        if (!s.hasEntries()) {
          continue;
        }
        for (Model.Entry e : s.entries()) {
          if (!e.dir() && Util.isSha256(e.sha())) {
            referenced.add(e.sha().toLowerCase(Locale.ROOT));
          }
        }
      }
    }

    ArchiveStore.ObjectIndex index = a.objects();
    Set<String> stored = index.sizes().keySet();
    int missing = 0;
    for (String sha : referenced) {
      if (!stored.contains(sha)) {
        missing++;
        problems.merge("objects", 1, Integer::sum);
        issues.add(new Issue("objects", sha, "object missing from store"));
      }
    }
    int orphans = 0;
    for (String sha : stored) {
      if (!referenced.contains(sha)) {
        orphans++;
        problems.merge("orphans", 1, Integer::sum);
        issues.add(new Issue("orphans", sha, "object not referenced by any manifest or history"));
      }
    }
    for (String bad : index.badNames()) {
      problems.merge("objects", 1, Integer::sum);
      issues.add(new Issue("objects", bad, "object file name is not a sha256"));
    }

    List<Check> checks = new ArrayList<>();
    int fatal = 0;
    for (Map.Entry<String, Integer> e : problems.entrySet()) {
      String name = e.getKey();
      int count = e.getValue();
      boolean isFatal = !name.equals("orphans") || strict;
      String status;
      if (count == 0) {
        status = "ok";
      } else if (isFatal) {
        status = "fail";
      } else {
        status = "warn";
      }
      if (isFatal) {
        fatal += count;
      }
      checks.add(new Check(name, status, count));
    }
    boolean pass = fatal == 0;
    return new Result(
        pass,
        strict,
        a.branches().size(),
        manifestsOk,
        entries,
        files,
        dirs,
        index.sizes().size(),
        index.totalBytes(),
        referenced.size(),
        missing,
        orphans,
        badSha,
        badPaths,
        duplicates,
        checks,
        issues,
        warnings);
  }

  public static int run(ArchiveStore a, I18n t, PrintStream out, boolean json, boolean strict, int jsonIndent) {
    Result r = check(a, t, strict);
    if (json) {
      out.println(toJson(t, r).dump(jsonIndent));
    } else {
      printText(a, t, out, r);
    }
    return r.pass() ? 0 : 1;
  }

  private static void printText(ArchiveStore a, I18n t, PrintStream out, Result r) {
    out.println(t.t("audit.title"));
    line(out, t, "report.root", a.root.toString());
    if (r.strict()) {
      line(out, t, "audit.strict", "");
    }
    out.println();
    line(out, t, "audit.count.branches", String.valueOf(r.branches()));
    line(out, t, "audit.count.manifests", String.valueOf(r.manifests()));
    line(out, t, "audit.count.entries", r.entries() + " (" + t.t("audit.count.files") + " "
        + r.files() + ", " + t.t("audit.count.dirs") + " " + r.dirs() + ")");
    line(out, t, "audit.count.objects", r.objects() + " (" + Util.humanSize(r.objectBytes()) + ")");
    line(out, t, "audit.count.referenced", String.valueOf(r.referenced()));
    line(out, t, "audit.count.missing", String.valueOf(r.missing()));
    line(out, t, "audit.count.orphans", String.valueOf(r.orphans()));
    line(out, t, "audit.count.badsha", String.valueOf(r.badSha()));
    line(out, t, "audit.count.badpaths", String.valueOf(r.badPaths()));
    line(out, t, "audit.count.duplicates", String.valueOf(r.duplicates()));

    out.println();
    out.println(t.t("audit.checks"));
    int w = 0;
    for (Check c : r.checks()) {
      w = Math.max(w, t.t(checkLabel(c.name())).length());
    }
    for (Check c : r.checks()) {
      String status = switch (c.status()) {
        case "ok" -> t.t("audit.check.ok");
        case "warn" -> t.t("audit.check.warn", c.problems());
        default -> t.t("audit.check.fail", c.problems());
      };
      out.println("  " + Util.padRight(t.t(checkLabel(c.name())), w + 2) + status);
    }

    if (!r.issues().isEmpty()) {
      out.println();
      out.println(t.t("audit.issues"));
      int shown = Math.min(r.issues().size(), MAX_ISSUES_PRINTED);
      for (int i = 0; i < shown; i++) {
        Issue issue = r.issues().get(i);
        out.println("  " + t.t("audit.issue", issue.check(), issue.subject() + " (" + issue.detail() + ")"));
      }
      if (r.issues().size() > shown) {
        out.println("  " + t.t("audit.more", r.issues().size() - shown));
      }
    }

    for (String warning : r.warnings()) {
      out.println(warning);
    }

    out.println();
    out.println(t.t("audit.result") + ": " + (r.pass() ? t.t("audit.pass") : t.t("audit.fail")));
  }

  private static String checkLabel(String name) {
    return switch (name) {
      case "manifests" -> "audit.check.manifests";
      case "objects" -> "audit.check.objects";
      case "sha" -> "audit.check.sha";
      case "paths" -> "audit.check.paths";
      case "duplicates" -> "audit.check.duplicates";
      case "orphans" -> "audit.check.orphans";
      default -> name;
    };
  }

  private static void line(PrintStream out, I18n t, String key, String value) {
    out.println("  " + Util.padRight(t.t(key), 18) + ": " + value);
  }

  private static Json toJson(I18n t, Result r) {
    Json root = Json.object();
    root.put("tool", t.t("app.name"));
    root.put("version", Cli.VERSION);
    root.put("command", "audit");
    root.put("pass", r.pass());
    root.put("strict", r.strict());

    Json counts = Json.object();
    counts.put("branches", r.branches());
    counts.put("manifests", r.manifests());
    counts.put("entries", r.entries());
    counts.put("files", r.files());
    counts.put("dirs", r.dirs());
    counts.put("objects", r.objects());
    counts.put("object_bytes", r.objectBytes());
    counts.put("referenced", r.referenced());
    counts.put("missing", r.missing());
    counts.put("orphans", r.orphans());
    counts.put("bad_sha256", r.badSha());
    counts.put("bad_paths", r.badPaths());
    counts.put("duplicates", r.duplicates());
    root.put("counts", counts);

    Json checks = Json.array();
    for (Check c : r.checks()) {
      Json item = Json.object();
      item.put("name", c.name());
      item.put("label", t.t(checkLabel(c.name())));
      item.put("status", c.status());
      item.put("problems", c.problems());
      checks.add(item);
    }
    root.put("checks", checks);

    Json issues = Json.array();
    for (Issue issue : r.issues()) {
      Json item = Json.object();
      item.put("check", issue.check());
      item.put("subject", issue.subject());
      item.put("detail", issue.detail());
      issues.add(item);
    }
    root.put("issues", issues);

    Json warnings = Json.array();
    for (String w : r.warnings()) {
      warnings.add(Json.of(w));
    }
    root.put("warnings", warnings);
    return root;
  }
}
