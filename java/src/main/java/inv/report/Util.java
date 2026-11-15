package inv.report;

import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Locale;

/** Small shared helpers: sha256 shape, human sizes, times, table padding, path rules. */
public final class Util {

  private Util() {}

  /** True when s is exactly 64 hexadecimal characters (any case). */
  public static boolean isSha256(String s) {
    if (s == null || s.length() != 64) {
      return false;
    }
    for (int i = 0; i < 64; i++) {
      char c = s.charAt(i);
      boolean hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
      if (!hex) {
        return false;
      }
    }
    return true;
  }

  public static String humanSize(long bytes) {
    if (bytes < 1024) {
      return bytes + " B";
    }
    double v = bytes;
    String[] units = {"KB", "MB", "GB", "TB", "PB"};
    int u = -1;
    do {
      v /= 1024.0;
      u++;
    } while (v >= 1024.0 && u < units.length - 1);
    return String.format(Locale.ROOT, "%.1f %s", v, units[u]);
  }

  public static String isoTime(long ms) {
    if (ms <= 0) {
      return "-";
    }
    return Instant.ofEpochMilli(ms).truncatedTo(ChronoUnit.SECONDS).toString();
  }

  public static String percent(double ratio) {
    if (Double.isNaN(ratio) || Double.isInfinite(ratio)) {
      return "n/a";
    }
    return String.format(Locale.ROOT, "%.2f%%", ratio * 100.0);
  }

  public static String padRight(String s, int width) {
    if (s.length() >= width) {
      return s;
    }
    return s + " ".repeat(width - s.length());
  }

  public static String padLeft(String s, int width) {
    if (s.length() >= width) {
      return s;
    }
    return " ".repeat(width - s.length()) + s;
  }

  /** Canonical branch path: leading '/', forward slashes, no trailing '/', no doubled slashes. */
  public static String normalizeBranchPath(String raw) {
    if (raw == null) {
      return "";
    }
    String s = raw.trim().replace('\\', '/');
    while (s.contains("//")) {
      s = s.replace("//", "/");
    }
    if (s.isEmpty()) {
      return "";
    }
    if (!s.startsWith("/")) {
      s = "/" + s;
    }
    while (s.length() > 1 && s.endsWith("/")) {
      s = s.substring(0, s.length() - 1);
    }
    return s;
  }

  /**
   * Archive-relative entry path: no leading slash, no backslash, no empty/'.'/'..' segment,
   * no drive letter or alternate data stream separator.
   */
  public static boolean validEntryPath(String p) {
    if (p == null || p.isEmpty()) {
      return false;
    }
    if (p.startsWith("/") || p.indexOf('\\') >= 0 || p.indexOf(':') >= 0) {
      return false;
    }
    if (p.contains("//")) {
      return false;
    }
    for (String seg : p.split("/", -1)) {
      if (seg.isEmpty() || seg.equals(".") || seg.equals("..")) {
        return false;
      }
    }
    return true;
  }

  public static String leaf(String path) {
    if (path == null) {
      return "";
    }
    int i = path.lastIndexOf('/');
    return i < 0 ? path : path.substring(i + 1);
  }

  /** Branch directory name derived from a branch path when the metadata has no id. */
  public static String deriveId(String path) {
    String s = normalizeBranchPath(path);
    if (s.isEmpty() || s.equals("/")) {
      return "root";
    }
    String id = s.substring(1).replace('/', '_').replace('\\', '_').replace(':', '_');
    return id.isEmpty() ? "root" : id;
  }

  /** Keep a branch id usable as a single directory name. */
  public static String safeId(String id) {
    if (id == null) {
      return "";
    }
    String s = id.replace('/', '_').replace('\\', '_').replace(':', '_');
    while (s.contains("..")) {
      s = s.replace("..", "_");
    }
    return s;
  }
}
