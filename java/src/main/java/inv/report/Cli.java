package inv.report;

import java.io.PrintStream;
import java.util.ArrayList;
import java.util.List;

/** Argument parsing and command dispatch. */
public final class Cli {

  public static final String VERSION = "1.0.0";

  private Cli() {}

  public static int run(String[] argv, PrintStream out) {
    I18n t = I18n.forLang(resolveLang(argv));

    String command = null;
    String archive = null;
    String branch = null;
    int limit = 50;
    int offset = 0;
    boolean json = false;
    boolean pretty = false;
    boolean strict = false;
    boolean help = false;

    int[] cursor = {0};
    while (cursor[0] < argv.length) {
      String raw = argv[cursor[0]];
      String name = raw;
      String inline = null;
      int eq = raw.indexOf('=');
      if (raw.startsWith("--") && eq > 0) {
        name = raw.substring(0, eq);
        inline = raw.substring(eq + 1);
      }
      switch (name) {
        case "-h", "--help" -> {
          help = true;
          cursor[0]++;
        }
        case "--json" -> {
          json = true;
          cursor[0]++;
        }
        case "--pretty" -> {
          pretty = true;
          cursor[0]++;
        }
        case "--strict" -> {
          strict = true;
          cursor[0]++;
        }
        case "--archive" -> {
          archive = take(argv, cursor, inline, name, t);
          cursor[0]++;
        }
        case "--branch" -> {
          branch = take(argv, cursor, inline, name, t);
          cursor[0]++;
        }
        case "--limit" -> {
          limit = positiveInt(take(argv, cursor, inline, name, t), name, t);
          cursor[0]++;
        }
        case "--offset" -> {
          offset = nonNegativeInt(take(argv, cursor, inline, name, t), name, t);
          cursor[0]++;
        }
        case "--lang" -> {
          take(argv, cursor, inline, name, t);
          cursor[0]++;
        }
        default -> {
          if (name.startsWith("-")) {
            throw new AppException(t.t("err.option.unknown", name));
          }
          if (command != null) {
            throw new AppException(t.t("err.arg.unknown", raw));
          }
          command = raw;
          cursor[0]++;
        }
      }
    }

    if (help || "help".equals(command)) {
      printUsage(t, out);
      return 0;
    }
    if (command == null) {
      throw new AppException(t.t("err.usage"));
    }
    int jsonIndent = pretty ? 2 : -1;
    if ("version".equals(command)) {
      printVersion(t, out, json, jsonIndent);
      return 0;
    }
    if (!"report".equals(command) && !"audit".equals(command) && !"inspect".equals(command)) {
      throw new AppException(t.t("err.usage"));
    }
    if (archive == null || archive.isBlank()) {
      throw new AppException(t.t("err.archive.required"));
    }

    ArchiveStore store = ArchiveStore.load(archive, t);
    return switch (command) {
      case "report" -> Report.run(store, t, out, json, jsonIndent);
      case "audit" -> Audit.run(store, t, out, json, strict, jsonIndent);
      default -> {
        if (branch == null || branch.isBlank()) {
          throw new AppException(t.t("err.branch.required"));
        }
        yield Inspect.run(store, t, out, branch, limit, offset, json, jsonIndent);
      }
    };
  }

  private static String take(String[] argv, int[] cursor, String inline, String name, I18n t) {
    if (inline != null) {
      return inline;
    }
    if (cursor[0] + 1 >= argv.length) {
      throw new AppException(t.t("err.option.value", name));
    }
    cursor[0]++;
    return argv[cursor[0]];
  }

  private static int positiveInt(String raw, String name, I18n t) {
    try {
      int v = Integer.parseInt(raw.trim());
      if (v <= 0) {
        throw new NumberFormatException("non-positive");
      }
      return v;
    } catch (NumberFormatException e) {
      throw new AppException(t.t("err.limit", name));
    }
  }

  private static int nonNegativeInt(String raw, String name, I18n t) {
    try {
      int v = Integer.parseInt(raw.trim());
      if (v < 0) {
        throw new NumberFormatException("negative");
      }
      return v;
    } catch (NumberFormatException e) {
      throw new AppException(t.t("err.limit", name));
    }
  }

  /** Explicit --lang is validated (a typo must not silently fall back to English). */
  private static I18n.Lang resolveLang(String[] argv) {
    String explicit = explicitLang(argv);
    if (explicit != null) {
      I18n.Lang lang = I18n.Lang.strict(explicit);
      if (lang == null) {
        throw new AppException(I18n.forLang(I18n.Lang.EN).t("err.lang", explicit));
      }
      return lang;
    }
    return I18n.Lang.parse(envLang());
  }

  private static String explicitLang(String[] argv) {
    for (int i = 0; i < argv.length; i++) {
      String a = argv[i];
      if (a.equals("--lang") && i + 1 < argv.length) {
        return argv[i + 1];
      }
      if (a.startsWith("--lang=")) {
        return a.substring("--lang=".length());
      }
    }
    return null;
  }

  private static String envLang() {
    String env = System.getenv("INV_LANG");
    if (env == null || env.isBlank()) {
      env = System.getenv("VD_LANG");
    }
    return env;
  }

  private static void printVersion(I18n t, PrintStream out, boolean json, int jsonIndent) {
    String javaVersion = System.getProperty("java.version", "?");
    String vm = System.getProperty("java.vm.name", "?");
    if (json) {
      Json o = Json.object();
      o.put("tool", t.t("app.name"));
      o.put("version", VERSION);
      o.put("java", javaVersion);
      o.put("vm", vm);
      o.put("lang", t.lang().code);
      out.println(o.dump(jsonIndent));
      return;
    }
    out.println(t.t("version.line", VERSION, javaVersion, vm));
  }

  private static void printUsage(I18n t, PrintStream out) {
    List<String> lines = new ArrayList<>();
    lines.add(t.t("usage.title") + ":");
    lines.add(t.t("usage.synopsis"));
    lines.add("");
    lines.add(t.t("usage.commands") + ":");
    lines.add(t.t("usage.report"));
    lines.add(t.t("usage.audit"));
    lines.add(t.t("usage.inspect"));
    lines.add(t.t("usage.version"));
    lines.add("");
    lines.add(t.t("usage.options") + ":");
    lines.add(t.t("usage.help"));
    lines.add(t.t("usage.lang"));
    lines.add(t.t("usage.pretty"));
    lines.add("");
    lines.add(t.t("usage.exit"));
    for (String line : lines) {
      out.println(line);
    }
  }
}
