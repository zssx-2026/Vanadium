package inv.report;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/**
 * Read-only view over a v1 archive (<root>/.inv).
 *
 * <p>The reader is deliberately tolerant: it accepts the canonical key names from
 * docs/DESIGN.md plus common aliases (snake_case / camelCase), a branch list as either an
 * array or an object map, and it never throws on a single bad branch: problems are surfaced
 * as {@code status} values and warnings so report/audit can print them instead of crashing.
 */
public final class ArchiveStore {

  /** Result of loading one branch manifest. status is "ok", "missing" or "invalid". */
  public record Manifest(Model.Snapshot snapshot, String status, String error) {}

  /** Objects currently on disk: sha (lowercase) -> stored bytes, plus non-sha file names. */
  public record ObjectIndex(Map<String, Long> sizes, List<String> badNames, long totalBytes) {}

  /** History snapshots plus the parse errors encountered while loading them. */
  public record HistoryLoad(List<Model.Snapshot> snapshots, List<String> errors) {}

  public final Path root;
  public final Path inv;
  public final String name;
  public final String id;
  public final String type;
  public final long createdMs;
  public final List<Model.Branch> branches;
  public final List<String> warnings = new ArrayList<>();

  private final I18n i18n;
  private final Map<String, Manifest> manifestCache = new HashMap<>();
  private ObjectIndex objectIndex;

  private ArchiveStore(Path root, Path inv, Json meta, I18n i18n) {
    this.root = root;
    this.inv = inv;
    this.i18n = i18n;
    this.name = firstString(meta, fallbackName(root), "name", "archive_name", "archiveName");
    this.id = firstString(meta, "", "id", "archive_id", "archiveId");
    this.type = firstString(meta, "unknown", "type", "archive_type", "archiveType");
    this.createdMs = firstTime(meta, "created_ms", "createdMs", "created", "created_at");
    this.branches = parseBranches(meta);
    if (!this.type.equals("full") && !this.type.equals("nodeonly") && !this.type.equals("least")) {
      warnings.add(i18n.t("report.warn", "unknown archive type '" + this.type + "'"));
    }
    Set<String> seenIds = new HashSet<>();
    for (Model.Branch b : this.branches) {
      if (!seenIds.add(b.id())) {
        warnings.add(i18n.t("report.warn", "duplicate branch id '" + b.id() + "' (" + b.path() + ")"));
      }
    }
  }

  public static ArchiveStore load(String archiveArg, I18n i18n) {
    if (archiveArg == null || archiveArg.isBlank()) {
      throw new AppException(i18n.t("err.archive.required"));
    }
    Path given;
    try {
      given = Paths.get(archiveArg).toAbsolutePath().normalize();
    } catch (RuntimeException e) {
      throw new AppException(i18n.t("err.archive.missing", archiveArg));
    }
    if (!Files.exists(given)) {
      throw new AppException(i18n.t("err.archive.missing", given));
    }
    // The kernel header (store.h: save_meta/meta_path) calls the file meta.json; docs/DESIGN.md
    // still shows archive.json. Accept both, preferring meta.json.
    String[] metaNames = {"meta.json", "archive.json"};
    Path inv = null;
    if (Files.isRegularFile(given)) {
      String fn = given.getFileName() == null ? "" : given.getFileName().toString();
      for (String name : metaNames) {
        if (fn.equals(name)) {
          inv = given.getParent();
          break;
        }
      }
    } else if (Files.isDirectory(given)) {
      for (String name : metaNames) {
        if (Files.isRegularFile(given.resolve(name))) {
          inv = given;
          break;
        }
      }
      if (inv == null) {
        for (String name : metaNames) {
          if (Files.isRegularFile(given.resolve(".inv").resolve(name))) {
            inv = given.resolve(".inv");
            break;
          }
        }
      }
    }
    if (inv == null) {
      throw new AppException(i18n.t("err.archive.meta", given));
    }

    Path metaPath = null;
    for (String name : metaNames) {
      Path candidate = inv.resolve(name);
      if (Files.isRegularFile(candidate)) {
        metaPath = candidate;
        break;
      }
    }
    if (metaPath == null) {
      throw new AppException(i18n.t("err.archive.meta", given));
    }
    String text;
    try {
      text = Files.readString(metaPath, StandardCharsets.UTF_8);
    } catch (IOException e) {
      throw new AppException(i18n.t("err.io", metaPath, e.getMessage()));
    }
    Json meta;
    try {
      meta = Json.parse(text);
    } catch (Json.ParseException e) {
      throw new AppException(i18n.t("err.archive.json", metaPath, e.getMessage()));
    }
    if (!meta.isObject()) {
      throw new AppException(i18n.t("err.archive.json", metaPath, "top-level value is not an object"));
    }
    Path root = inv.getParent() == null ? inv : inv.getParent();
    return new ArchiveStore(root, inv, meta, i18n);
  }

  /** Branches declared by archive.json, in declaration order. */
  public List<Model.Branch> branches() {
    return branches;
  }

  public Model.Branch find(String branchPath) {
    String want = Util.normalizeBranchPath(branchPath);
    for (Model.Branch b : branches) {
      if (b.path().equals(want)) {
        return b;
      }
    }
    for (Model.Branch b : branches) {
      if (b.name().equals(branchPath) || b.name().equals(want)) {
        return b;
      }
    }
    return null;
  }

  public Path manifestPath(Model.Branch b) {
    Path base = inv.resolve("branches");
    String primary = Util.safeId(b.id());
    Path direct = base.resolve(primary).resolve("manifest.json");
    if (Files.isRegularFile(direct)) {
      return direct;
    }
    String derived = Util.safeId(Util.deriveId(b.path()));
    if (!derived.equals(primary)) {
      Path alt = base.resolve(derived).resolve("manifest.json");
      if (Files.isRegularFile(alt)) {
        return alt;
      }
    }
    return direct;
  }

  public Manifest manifest(Model.Branch b) {
    Manifest cached = manifestCache.get(b.path());
    if (cached != null) {
      return cached;
    }
    Manifest result;
    Path p = manifestPath(b);
    if (!Files.isRegularFile(p)) {
      result = new Manifest(null, "missing", p.toString());
    } else {
      try {
        String text = Files.readString(p, StandardCharsets.UTF_8);
        Json j = Json.parse(text);
        if (!j.isObject()) {
          throw new Json.ParseException("top-level value is not an object");
        }
        result = new Manifest(parseSnapshot(j, b.path()), "ok", "");
      } catch (Json.ParseException e) {
        result = new Manifest(null, "invalid", e.getMessage());
      } catch (IOException e) {
        result = new Manifest(null, "invalid", e.getMessage());
      }
    }
    manifestCache.put(b.path(), result);
    return result;
  }

  /** History files for a branch, ordered by numeric snapshot sequence. */
  public List<Path> historyFiles(Model.Branch b) {
    Path dir = manifestPath(b).getParent();
    List<Path> out = new ArrayList<>();
    if (dir == null) {
      return out;
    }
    Path history = dir.resolve("history");
    if (!Files.isDirectory(history)) {
      return out;
    }
    try (DirectoryStream<Path> ds = Files.newDirectoryStream(history, "*.json")) {
      for (Path p : ds) {
        out.add(p);
      }
    } catch (IOException e) {
      warnings.add(i18n.t("report.warn", "cannot list " + history + ": " + e.getMessage()));
      return out;
    }
    out.sort((x, y) -> {
      long sx = seqOf(x);
      long sy = seqOf(y);
      if (sx != sy) {
        return Long.compare(sx, sy);
      }
      return x.getFileName().toString().compareTo(y.getFileName().toString());
    });
    return out;
  }

  public HistoryLoad history(Model.Branch b) {
    List<Model.Snapshot> snaps = new ArrayList<>();
    List<String> errors = new ArrayList<>();
    for (Path p : historyFiles(b)) {
      try {
        Json j = Json.parse(Files.readString(p, StandardCharsets.UTF_8));
        if (!j.isObject()) {
          throw new Json.ParseException("top-level value is not an object");
        }
        snaps.add(parseSnapshot(j, b.path()));
      } catch (Json.ParseException | IOException e) {
        errors.add(p.getFileName() + ": " + e.getMessage());
      }
    }
    return new HistoryLoad(snaps, errors);
  }

  /** Walk the object store. Tolerates a flat layout and reports non-sha file names. */
  public ObjectIndex objects() {
    if (objectIndex != null) {
      return objectIndex;
    }
    Map<String, Long> sizes = new TreeMap<>();
    List<String> badNames = new ArrayList<>();
    Path objRoot = inv.resolve("objects");
    if (Files.isDirectory(objRoot)) {
      try (DirectoryStream<Path> ds = Files.newDirectoryStream(objRoot)) {
        for (Path p : ds) {
          if (Files.isDirectory(p)) {
            try (DirectoryStream<Path> inner = Files.newDirectoryStream(p)) {
              for (Path f : inner) {
                if (Files.isRegularFile(f)) {
                  addObject(objRoot, f, sizes, badNames);
                } else {
                  badNames.add(objRoot.relativize(f).toString().replace('\\', '/'));
                }
              }
            }
          } else if (Files.isRegularFile(p)) {
            addObject(objRoot, p, sizes, badNames);
          }
        }
      } catch (IOException e) {
        warnings.add(i18n.t("report.warn", "cannot list " + objRoot + ": " + e.getMessage()));
      }
    }
    long total = 0;
    for (long v : sizes.values()) {
      total += v;
    }
    objectIndex = new ObjectIndex(sizes, badNames, total);
    return objectIndex;
  }

  private void addObject(Path objRoot, Path f, Map<String, Long> sizes, List<String> badNames) {
    String name = f.getFileName().toString();
    if (!Util.isSha256(name)) {
      badNames.add(objRoot.relativize(f).toString().replace('\\', '/'));
      return;
    }
    long size = 0;
    try {
      size = Files.size(f);
    } catch (IOException ignored) {
      // size stays 0; audit reports the object as present but empty-sized
    }
    sizes.put(name.toLowerCase(Locale.ROOT), size);
  }

  private List<Model.Branch> parseBranches(Json meta) {
    List<Model.Branch> out = new ArrayList<>();
    Json bj = meta.get("branches");
    if (bj == null) {
      bj = meta.get("branch_tree");
    }
    if (bj == null) {
      return out;
    }
    if (bj.isArray()) {
      for (Json item : bj.items()) {
        if (item.isObject()) {
          out.add(parseBranch(item, null));
        }
      }
    } else if (bj.isObject()) {
      for (Map.Entry<String, Json> e : bj.fields().entrySet()) {
        if (e.getValue().isObject()) {
          out.add(parseBranch(e.getValue(), e.getKey()));
        }
      }
    } else {
      warnings.add(i18n.t("report.warn", "branch metadata is not an array or object"));
    }
    return out;
  }

  private Model.Branch parseBranch(Json j, String pathFromKey) {
    String path = Util.normalizeBranchPath(firstString(j, pathFromKey, "path", "branch", "id_path"));
    if (path.isEmpty()) {
      path = Util.normalizeBranchPath(firstString(j, "/" + firstString(j, "branch", "name"), "name"));
    }
    String name = firstString(j, Util.leaf(path), "name", "label");
    String id = Util.safeId(firstString(j, Util.deriveId(path), "id", "bid", "dir"));
    String parent = Util.normalizeBranchPath(firstString(j, "/", "parent", "parent_path"));
    if (parent.isEmpty()) {
      parent = "/";
    }
    List<String> children = new ArrayList<>();
    Json cj = j.get("children");
    if (cj != null && cj.isArray()) {
      for (Json c : cj.items()) {
        if (c.isString()) {
          children.add(Util.normalizeBranchPath(c.asString("")));
        }
      }
    }
    return new Model.Branch(
        id,
        path,
        name,
        parent,
        firstTime(j, "created_ms", "createdMs", "created"),
        firstLong(j, 0L, "head_seq", "headSeq", "seq", "head"),
        firstBool(j, false, "is_node", "isNode", "node"),
        children,
        firstBool(j, false, "system", "is_system", "isSystem"));
  }

  private Model.Snapshot parseSnapshot(Json j, String fallbackBranch) {
    Json entriesJson = j.get("entries");
    if (entriesJson == null) {
      entriesJson = j.get("files");
    }
    if (entriesJson == null) {
      Json nested = j.get("snapshot");
      if (nested != null && nested.isObject()) {
        entriesJson = nested.get("entries");
      }
    }
    List<Model.Entry> entries = new ArrayList<>();
    boolean hasEntries = entriesJson != null && entriesJson.isArray();
    if (hasEntries) {
      for (Json e : entriesJson.items()) {
        if (e.isObject()) {
          entries.add(parseEntry(e));
        }
      }
    }
    return new Model.Snapshot(
        firstLong(j, 0L, "seq", "sequence", "number"),
        firstString(j, fallbackBranch, "branch", "path"),
        firstTime(j, "time_ms", "timeMs", "time"),
        firstString(j, "", "message", "msg", "subject"),
        firstString(j, "", "parent_sha", "parentSha", "parent", "parent_id"),
        firstString(j, "", "id", "sha", "snapshot_id"),
        entries,
        hasEntries);
  }

  private Model.Entry parseEntry(Json e) {
    String type = firstString(e, "", "type", "kind");
    boolean dir = firstBool(e, false, "dir", "is_dir", "isDir") || type.equalsIgnoreCase("dir")
        || type.equalsIgnoreCase("directory");
    String path = firstString(e, "", "path", "name");
    if (path.indexOf('\\') >= 0) {
      path = path.replace('\\', '/');
    }
    return new Model.Entry(
        path,
        dir,
        firstLong(e, 0L, "size", "bytes", "length"),
        firstString(e, "", "sha", "sha256", "hash"),
        crcValue(e),
        firstTime(e, "mtime", "mtime_ms", "mtimeMs", "time"),
        modeValue(e.get("mode")));
  }

  private static long crcValue(Json e) {
    Json v = e.get("crc");
    if (v == null) {
      v = e.get("crc64");
    }
    if (v == null) {
      return 0L;
    }
    if (v.isNumber()) {
      return v.asLong(0L);
    }
    String s = v.asString("").trim();
    if (s.isEmpty()) {
      return 0L;
    }
    try {
      return Long.parseUnsignedLong(s, 16);
    } catch (NumberFormatException ignored) {
      try {
        return Long.parseUnsignedLong(s);
      } catch (NumberFormatException ignoredAgain) {
        return 0L;
      }
    }
  }

  private static int modeValue(Json v) {
    if (v == null) {
      return 0644;
    }
    if (v.isNumber()) {
      long m = v.asLong(0644L);
      return m < 0 ? 0644 : (int) m;
    }
    String s = v.asString("").trim();
    if (s.isEmpty()) {
      return 0644;
    }
    try {
      return s.startsWith("0") ? Integer.parseInt(s, 8) : Integer.parseInt(s);
    } catch (NumberFormatException ignored) {
      return 0644;
    }
  }

  private static String fallbackName(Path root) {
    Path n = root.getFileName();
    return n == null ? "archive" : n.toString();
  }

  private static long seqOf(Path p) {
    String n = p.getFileName().toString();
    int dot = n.lastIndexOf('.');
    String stem = dot > 0 ? n.substring(0, dot) : n;
    try {
      return Long.parseLong(stem);
    } catch (NumberFormatException e) {
      return Long.MAX_VALUE;
    }
  }

  private static String firstString(Json j, String def, String... keys) {
    for (String k : keys) {
      Json v = j.get(k);
      if (v != null && v.isString() && !v.asString("").isEmpty()) {
        return v.asString("");
      }
    }
    return def == null ? "" : def;
  }

  private static long firstLong(Json j, long def, String... keys) {
    for (String k : keys) {
      Json v = j.get(k);
      if (v != null && v.isNumber()) {
        return v.asLong(def);
      }
    }
    return def;
  }

  private static boolean firstBool(Json j, boolean def, String... keys) {
    for (String k : keys) {
      Json v = j.get(k);
      if (v != null && (v.isBool() || v.isNumber())) {
        return v.asBool(def);
      }
    }
    return def;
  }

  private static long firstTime(Json j, String... keys) {
    for (String k : keys) {
      Json v = j.get(k);
      if (v == null) {
        continue;
      }
      if (v.isNumber()) {
        return v.asLong(0L);
      }
      if (v.isString() && !v.asString("").isBlank()) {
        try {
          return Instant.parse(v.asString("")).toEpochMilli();
        } catch (RuntimeException ignored) {
          return 0L;
        }
      }
    }
    return 0L;
  }
}
