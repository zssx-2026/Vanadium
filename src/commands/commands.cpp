#include "vanadium/commands.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/crypto.h"
#include "vanadium/gen.h"
#include "vanadium/registry.h"
#include "vanadium/remote.h"
#include "vanadium/store.h"
#include "vanadium/vd.h"

namespace vd {
namespace {

const char* kVersion = "0.4.0";

/* Walk up from start looking for an archive root (meta.json). */
std::string find_archive_root(const std::string& start) {
  std::string cur = fs::absolute(start);
  for (int depth = 0; depth < 64; ++depth) {
    if (Archive::looks_like_archive(cur)) return cur;
    const std::string up = fs::parent(cur);
    if (up.empty() || up == cur) break;
    cur = up;
  }
  return std::string();
}

Archive open_here(App& app, const std::string& start = std::string()) {
  const std::string base = start.empty() ? app.cwd : start;
  const std::string root = find_archive_root(base);
  if (root.empty()) fail(3100, msg("store.not_archive"), fs::absolute(base));
  return Archive::open(root);
}

std::string default_settings_path(const App& app) { return fs::join(app.home, "settings.json"); }

void load_settings(App& app) {
  fs::mkdirs(app.home);
  app.settings.load(default_settings_path(app));
  if (app.lang_locked) return;
  const std::string code = app.settings.get("lang");
  if (!code.empty()) {
    app.lang = lang_from_code(code);
    set_language(app.lang);
  }
}

void save_settings(App& app) { app.settings.save(default_settings_path(app)); }

/* Render "12 files, 3 branches" style count lines plus elapsed time. */
void done_line(App& app, const std::string& what, std::size_t count, i64 started_ms) {
  app.ui.line(what + " done.");
  app.ui.line("  " + std::to_string(count) + " " + msg("ui.items") + ", " +
              human_duration(now_ms() - started_ms));
}

std::string short_sha(const std::string& sha, std::size_t n = 12) {
  return sha.size() <= n ? sha : sha.substr(0, n);
}

/* File tree of one branch snapshot, indented by depth. */
void print_file_tree(App& app, const Archive& a, const std::string& branch_path, int max_depth,
                     bool line_mode) {
  auto snap = a.head(branch_path);
  if (!snap) {
    app.ui.line(msgf("store.no_snapshot", branch_path));
    return;
  }
  // Snapshot entries are stored relative to the branch root, so no prefix filter
  // is needed here: a branch snapshot already contains only its own subtree.
  std::map<std::string, std::vector<const Entry*>> kids;
  for (const auto& e : snap->entries) {
    const std::size_t slash = e.path.find('/');
    const std::string key = slash == std::string::npos ? std::string() : e.path.substr(0, slash);
    kids[key].push_back(&e);
  }
  /* Collect first, then pad: column alignment has to use the terminal display
     width (generated width tables) instead of byte counts, otherwise CJK names
     shift the size column. */
  struct Row {
    std::string label;
    std::string size;
    std::string sha;
    bool dir = false;
  };
  std::vector<Row> rows;
  std::function<void(const std::string&, int)> walk = [&](const std::string& dir, int depth) {
    auto it = kids.find(dir);
    if (it == kids.end()) return;
    std::vector<const Entry*> items = it->second;
    std::sort(items.begin(), items.end(), [](const Entry* x, const Entry* y) {
      if (x->dir != y->dir) return x->dir;
      return x->path < y->path;
    });
    for (const Entry* e : items) {
      const std::string& rel = e->path;
      std::string pad;
      for (int i = 0; i < depth; ++i) pad += "  ";
      const std::string leaf = rel.substr(rel.find_last_of('/') + 1);
      const std::string root_prefix = branch_path == "/" ? std::string("/") : branch_path + "/";
      const std::string shown = line_mode ? root_prefix + rel : leaf;
      if (e->dir) {
        rows.push_back(Row{pad + shown + "/", std::string(), std::string(), true});
        if (max_depth >= 0 && depth >= max_depth) continue;
        walk(rel, depth + 1);
      } else {
        rows.push_back(Row{pad + shown, human_size(static_cast<i64>(e->size)),
                           short_sha(e->sha, 12), false});
      }
    }
  };
  walk(std::string(), 0);
  std::size_t widest = 0;
  for (const auto& r : rows) {
    const std::size_t w = static_cast<std::size_t>(gen::text_width(r.label));
    if (w > widest) widest = w;
  }
  app.ui.line(branch_path + (snap->entries.empty() ? "  (empty)" : ""));
  for (const auto& r : rows) {
    std::string line = r.label;
    const std::size_t w = static_cast<std::size_t>(gen::text_width(r.label));
    if (!r.dir) {
      line.append(widest + 2 - w, ' ');
      line += r.size + "  " + r.sha;
    }
    app.ui.line(line);
  }
}

void print_branch_tree(App& app, const Archive& a, bool tree, bool releases, int max_depth) {
  std::vector<BranchInfo> all = a.branches();
  std::sort(all.begin(), all.end(), [](const BranchInfo& x, const BranchInfo& y) {
    return x.path < y.path;
  });
  if (!tree) {
    for (const auto& b : all) {
      app.ui.line(b.path + "  @" + std::to_string(b.head_seq) + "  " + short_sha(b.id, 12) +
                  (b.system ? "  [system]" : ""));
    }
    if (releases) {
      app.ui.line("releases/tag/v1.0  [system]");
      app.ui.line("releases/rawdata/v1.0  [system]");
    }
    return;
  }
  app.ui.line("/");
  app.ui.line("branches/");
  for (const auto& b : all) {
    if (b.path == "/") continue;
    const std::string rel = b.path.substr(1);
    const std::size_t depth = static_cast<std::size_t>(std::count(rel.begin(), rel.end(), '/'));
    if (max_depth >= 0 && static_cast<int>(depth) > max_depth) continue;
    std::string line = "  ";
    for (std::size_t i = 0; i < depth; ++i) line += "|   ";
    line += "|-- " + b.name;
    if (b.head_seq > 0) line += "  @" + std::to_string(b.head_seq);
    app.ui.line(line);
  }
  if (releases) {
    app.ui.line("releases/tag/v1.0");
    app.ui.line("releases/rawdata/v1.0");
  }
}

/* Options that consume the following token as their value. need_arg uses this
   list to skip options when locating positional arguments, so "inv search -regex
   foo" and "inv search foo -regex" resolve to the same pattern. */
bool flag_takes_value(const std::string& flag) {
  static const std::set<std::string> kValueFlags = {
      "-alias", "-at",   "-branch", "-content", "-crc64", "-depth",  "-encoding", "-filter",
      "-format", "-keep", "-lang",   "-limit",   "-message", "-n",    "-notes",
      "-out",    "-path", "-seconds", "-set",    "-sha256", "-size",  "-thread",   "-threads",
      "-title",  "-token", "-type",  "-url"};
  return kValueFlags.find(flag) != kValueFlags.end();
}

}  // namespace

/* ---------------------------------------------------------------- helpers */

std::vector<std::string> positionals(const std::vector<std::string>& args) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (i > 0 && !a.empty() && a[0] == '-') {
      if (a.find('=') == std::string::npos && flag_takes_value(a)) ++i;
      continue;
    }
    out.push_back(a);
  }
  return out;
}

std::string need_arg(const std::vector<std::string>& args, std::size_t i, const char* name) {
  const std::vector<std::string> pos = positionals(args);
  if (i >= pos.size() || pos[i].empty()) {
    Failure f;
    f.code = 1001;
    f.message = std::string(msg("cli.missing_arg")) + ": " + name;
    throw f;
  }
  return pos[i];
}

std::string opt_value(const std::vector<std::string>& args, const std::string& key,
                      const std::string& def) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == key) {
      if (i + 1 < args.size() && !starts_with(args[i + 1], "-")) return args[i + 1];
      return std::string();
    }
    if (starts_with(args[i], key + "=")) return args[i].substr(key.size() + 1);
  }
  return def;
}

bool has_flag(const std::vector<std::string>& args, const std::string& flag) {
  for (const auto& a : args) {
    if (a == flag) return true;
    if (starts_with(a, flag + "=")) return true;
  }
  return false;
}

void print_help(App& app, const std::string& command) {
  if (command.empty()) {
    app.ui.line("inv " + std::string(kVersion) + " - integrated version control");
    app.ui.line("");
    app.ui.line("usage: inv <command> [args]");
    app.ui.line("");
    const int n = gen::command_count();
    for (int i = 0; i < n; ++i) {
      const char* name = gen::command_name(i);
      const char* sum = gen::command_summary(i, static_cast<int>(app.lang));
      if (!name) continue;
      std::string line = "  " + std::string(name);
      while (line.size() < 16) line.push_back(' ');
      line += sum ? sum : "";
      app.ui.line(line);
    }
    app.ui.line("");
    app.ui.line("global options:");
    const int gn = gen::command_option_count(-1);
    for (int i = 0; i < gn; ++i) {
      const char* flag = gen::command_option_flag(-1, i);
      const char* arg = gen::command_option_arg(-1, i);
      const char* text = gen::command_option(-1, i, static_cast<int>(app.lang));
      if (!flag) continue;
      std::string line = "  " + std::string(flag);
      if (arg && *arg) line += " " + std::string(arg);
      while (line.size() < 24) line.push_back(' ');
      line += text ? text : "";
      app.ui.line(line);
    }
    app.ui.line("");
    app.ui.line("run 'inv help <command>' for details");
    return;
  }
  const int n = gen::command_count();
  for (int i = 0; i < n; ++i) {
    const char* name = gen::command_name(i);
    if (!name || command != name) continue;
    const char* sum = gen::command_summary(i, static_cast<int>(app.lang));
    const char* usage = gen::command_usage(i, static_cast<int>(app.lang));
    app.ui.line(std::string(name) + " - " + (sum ? sum : ""));
    app.ui.line(std::string("usage: ") + (usage ? usage : name));
    const int opts = gen::command_option_count(i);
    if (opts > 0) {
      app.ui.line("options:");
      for (int k = 0; k < opts; ++k) {
        const char* flag = gen::command_option_flag(i, k);
        const char* arg = gen::command_option_arg(i, k);
        const char* text = gen::command_option(i, k, static_cast<int>(app.lang));
        if (!flag) continue;
        std::string line = "  " + std::string(flag);
        if (arg && *arg) line += " " + std::string(arg);
        while (line.size() < 24) line.push_back(' ');
        line += text ? text : "";
        app.ui.line(line);
      }
    }
    return;
  }
  app.ui.warn(std::string(msg("cli.no_command")) + ": " + command);
}

/* --------------------------------------------------------------- commands */

int cmd_version(App& app, const std::vector<std::string>&) {
  app.ui.line("inv " + std::string(kVersion) + " (vanadium " + vd_version() + ", abi " + vd_abi() + ")");
  app.ui.line(std::string("kernel lines: ") + std::to_string(gen::self_test_count()) + " self-test vectors");
  return 0;
}

int cmd_help(App& app, const std::vector<std::string>& args) {
  print_help(app, args.size() > 1 ? args[1] : std::string());
  return 0;
}

int cmd_set(App& app, const std::vector<std::string>& args) {
  load_settings(app);
  if (has_flag(args, "-unset")) {
    const std::vector<std::string> pos = positionals(args);
    if (pos.size() < 2) fail(1001, std::string(msg("cli.missing_arg")) + ": <key>");
    app.settings.unset(pos[1]);
    save_settings(app);
    app.ui.line("unset " + pos[1] + ", default restored");
    return 0;
  }
  if (has_flag(args, "-list") || args.size() < 2) {
    app.ui.line(msg("set.list"));
    for (const auto& d : Settings::defs()) {
      if (d.dynamic) {
        app.ui.line("  " + d.key + "<name> = <url>   (dynamic)");
        continue;
      }
      std::string line = "  " + d.key;
      const int pad = 28 - gen::text_width(line);
      for (int i = 0; i < pad; ++i) line.push_back(' ');
      line += "= " + app.settings.get(d.key);
      app.ui.line(line);
    }
    return 0;
  }
  const std::vector<std::string> pos = positionals(args);
  const std::string key = pos[1];
  if (pos.size() < 3) {
    app.ui.line("  " + key + " = " + app.settings.get(key));
    return 0;
  }
  app.settings.set(key, pos[2]);
  save_settings(app);
  app.ui.line(msgf2("set.saved", key, pos[2]));
  return 0;
}

int cmd_init(App& app, const std::vector<std::string>& args) {
  const i64 started = now_ms();
  std::string target = args.size() > 1 && !starts_with(args[1], "-") ? args[1] : app.cwd;
  target = fs::absolute(target);
  if (Archive::looks_like_archive(target)) {
    app.ui.warn(msg("init.exists"));
    return 0;
  }
  fs::mkdirs(target);
  Archive a = Archive::create(target, fs::filename(target), ArchiveType::NodeOnly);
  /* The workspace registry lives in <workspace>/.inv and is created here and
     nowhere else; every registered branch gets a <id>.inv record in it. */
  ensure_registry_dir(target);
  RegistryEntry root_record;
  root_record.archive = a.id();
  root_record.name = a.name();
  root_record.type = archive_type_name(a.type());
  root_record.path = "/";
  root_record.dir = target;
  root_record.created_ms = a.created_ms();
  root_record.registered_ms = now_ms();
  const auto root_branch = a.find("/");
  root_record.id = root_branch ? root_branch->id : std::string();
  root_record.head_seq = 0;
  if (!root_record.id.empty()) {
    std::string reg_err;
    if (!write_registry(target, root_record, &reg_err)) {
      app.ui.warn("registry write failed: " + reg_err);
    }
  }
  app.ui.line(msgf("init.created", a.root()));
  done_line(app, "init", a.branches().size(), started);
  return 0;
}

int cmd_status(App& app, const std::vector<std::string>& args) {
  const std::vector<std::string> pos = positionals(args);
  Archive a = open_here(app, pos.size() > 1 ? pos[1] : std::string());
  if (has_flag(args, "-short")) {
    /* Compact form for scripting: one line per path, then a one-line summary. */
    std::vector<std::string> deleted;
    std::set<std::string> added;
    auto changed = a.scan_working_tree(a.root(), &deleted, &added);
    for (const auto& e : changed) {
      app.ui.line(std::string(added.count(e.path) ? "A  " : "M  ") + e.path);
    }
    for (const auto& d : deleted) app.ui.line("D  " + d);
    app.ui.line(std::to_string(changed.size()) + " " + msg("ui.changed") + ", " +
                std::to_string(deleted.size()) + " " + msg("ui.deleted"));
    return 0;
  }
  app.ui.line(std::string(msg("status.archive")) + ": " + a.name() + "  (" + a.root() + ")");
  app.ui.line(std::string(msg("status.type")) + ": " + archive_type_name(a.type()));
  const std::size_t nb = a.branches().size();
  app.ui.line(std::string(msg("status.branches")) + ": " + std::to_string(nb));
  auto h = a.head("/");
  if (h) {
    app.ui.line(std::string(msg("status.head")) + ": @" + std::to_string(h->seq) + " " +
                iso_time(h->time_ms) + "  " + h->message);
    app.ui.line(std::string(msg("status.files")) + ": " + std::to_string(h->entries.size()));
  } else {
    app.ui.line(std::string(msg("status.head")) + ": " + msg("status.empty"));
  }
  app.ui.line(std::string(msg("status.objects")) + ": " +
              std::to_string(a.objects().all_objects().size()) + " / " +
              human_size(static_cast<i64>(a.objects().total_bytes())));

  std::vector<std::string> deleted;
  auto changed = a.scan_working_tree(a.root(), &deleted);
  app.ui.line(std::string(msg("status.changes")) + ": " + std::to_string(changed.size()) + " " +
              msg("ui.changed") + ", " + std::to_string(deleted.size()) + " " + msg("ui.deleted"));
  return 0;
}

int cmd_log(App& app, const std::vector<std::string>& args) {
  Archive a = open_here(app);
  const std::string branch = opt_value(args, "-branch", "/");
  std::size_t limit = 20;
  const std::string limit_text = opt_value(args, "-limit", opt_value(args, "-n", std::string()));
  if (!limit_text.empty()) limit = static_cast<std::size_t>(std::atoll(limit_text.c_str()));
  const bool stat = has_flag(args, "-stat");
  auto hist = a.history(branch, limit);
  if (hist.empty()) {
    app.ui.line(msg("log.empty"));
    return 0;
  }
  const bool oneline = has_flag(args, "-oneline");
  for (auto it = hist.rbegin(); it != hist.rend(); ++it) {
    if (stat) {
      u64 bytes = 0;
      for (const auto& e : it->entries) bytes += e.size;
      app.ui.line("@" + std::to_string(it->seq) + "  " + short_sha(it->id) + "  " +
                  std::to_string(it->entries.size()) + " " + msg("ui.items") + ", " +
                  human_size(static_cast<i64>(bytes)) + "  " + it->message);
    } else if (oneline) {
      app.ui.line("@" + std::to_string(it->seq) + " " + short_sha(it->id, 8) + " " + it->message);
    } else {
      app.ui.line("@" + std::to_string(it->seq) + "  " + short_sha(it->id) + "  " +
                  iso_time(it->time_ms) + "  " + it->message);
    }
  }
  return 0;
}

int cmd_diff(App& app, const std::vector<std::string>& args) {
  Archive a = open_here(app);
  const bool name_only = has_flag(args, "-name-only");
  const bool stat_only = has_flag(args, "-stat");
  const std::string path_glob = opt_value(args, "-path", std::string());
  std::vector<std::string> deleted;
  std::set<std::string> added;
  auto changed = a.scan_working_tree(a.root(), &deleted, &added);
  const auto wanted = [&](const std::string& path) {
    return path_glob.empty() || gen::glob_match(path_glob, path);
  };
  std::size_t shown_changed = 0;
  std::size_t shown_deleted = 0;
  u64 bytes = 0;
  for (const auto& e : changed) {
    if (!wanted(e.path)) continue;
    ++shown_changed;
    bytes += e.size;
    if (name_only) {
      app.ui.line(e.path);
    } else if (!stat_only) {
      app.ui.line(std::string(added.count(e.path) ? "A  " : "M  ") + e.path + "  " +
                  human_size(static_cast<i64>(e.size)));
    }
  }
  for (const auto& d : deleted) {
    if (!wanted(d)) continue;
    ++shown_deleted;
    if (!stat_only) app.ui.line(name_only ? d : "D  " + d);
  }
  if (shown_changed == 0 && shown_deleted == 0) {
    app.ui.line(msg("ui.clean"));
    return 0;
  }
  if (stat_only) {
    app.ui.line(std::to_string(shown_changed) + " " + msg("ui.changed") + ", " +
                std::to_string(shown_deleted) + " " + msg("ui.deleted") + ", " +
                human_size(static_cast<i64>(bytes)));
  }
  return 0;
}

int cmd_reg(App& app, const std::vector<std::string>& args) {
  const i64 started = now_ms();
  /* Registering requires an initialised workspace: the registry directory
     <cwd>/.inv must already exist. It is never created implicitly. */
  if (!workspace_initialized(app.cwd)) {
    fail(3014, "the current working directory is not an initialised inv workspace (run \"inv init\" first)", app.cwd);
  }
  Archive a = open_here(app);
  const std::string raw = need_arg(args, 1, "<path>");
  const std::string abs = fs::absolute(raw);
  if (!fs::is_dir(abs)) fail(3101, msg("store.no_dir"), abs);

  /* Spec phrasing: the two-phase registration banner. The shell animates the
     trailing dots on the status line; the kernel emits the base text and then
     the settled line for each phase. */
  app.ui.status("reg.search", "Searching folder");
  app.ui.clear_status();
  app.ui.line("The folder has been searched.");

  std::string branch_path = opt_value(args, "-branch", std::string());
  if (branch_path.empty()) {
    /* Derive the branch path from the directory and canonicalize it: the raw
       relative text starts with a separator, which would otherwise show up as
       "//docs" in the confirmation line. */
    branch_path = gen::normalize_path(abs.substr(a.root().size()));
    if (branch_path.empty() || branch_path == ".") branch_path = "/";
  } else {
    branch_path = gen::normalize_path(branch_path);
  }
  const std::string type_text = opt_value(args, "-type", archive_type_name(a.type()));
  const ArchiveType type = archive_type_parse(type_text);
  if (!a.branch_exists(branch_path)) a.create_branch(branch_path, "");

  std::vector<std::string> deleted;
  auto entries = a.scan_working_tree(abs, &deleted);
  for (const auto& e : entries) {
    if (e.dir) continue;
    std::string data = fs::read_file(fs::join(abs, e.path));
    a.objects().put(data);
  }
  const std::string id = a.commit(branch_path, entries, opt_value(args, "-message", "register"));

  app.ui.status("reg.register", "Registering folder");
  app.ui.clear_status();
  app.ui.line("The folder has been registered.");

  /* Persist the registry record <cwd>/.inv/<branch id>.inv. */
  const auto info = a.find(branch_path);
  if (info) {
    RegistryEntry record;
    record.id = info->id;
    record.archive = a.id();
    record.name = a.name();
    record.type = archive_type_name(type);
    record.path = branch_path;
    record.dir = abs;
    record.created_ms = info->created_ms;
    record.registered_ms = now_ms();
    record.head_seq = info->head_seq;
    std::string reg_err;
    if (!write_registry(app.cwd, record, &reg_err)) {
      app.ui.warn("registry write failed: " + reg_err);
    }
  }
  app.ui.line("Registered \"" + branch_path + "\" (" + std::to_string(entries.size()) +
              " items, id " + short_sha(info ? info->id : id) + ")");
  done_line(app, "reg", entries.size(), started);
  return 0;
}

int cmd_unreg(App& app, const std::vector<std::string>& args) {
  if (!workspace_initialized(app.cwd)) {
    fail(3014, "the current working directory is not an initialised inv workspace (run \"inv init\" first)", app.cwd);
  }
  Archive a = open_here(app);
  const std::string raw = need_arg(args, 1, "<path>");
  std::string branch_path = gen::normalize_path(raw);
  if (fs::is_dir(fs::absolute(raw)) || fs::is_file(fs::absolute(raw))) {
    branch_path = gen::normalize_path(fs::absolute(raw).substr(a.root().size()));
    if (branch_path.empty() || branch_path == ".") branch_path = "/";
  }
  app.ui.status("unreg.search", "Searching folder");
  app.ui.clear_status();
  app.ui.line("The folder has been searched.");

  const auto info = a.find(branch_path);
  a.remove_branch(branch_path, has_flag(args, "-recursive"));

  app.ui.status("unreg.unregister", "Unregistering folder");
  app.ui.clear_status();
  app.ui.line("The folder has been unregistered.");

  /* Drop the registry record when one exists for this branch. */
  if (info) remove_registry(app.cwd, info->id);
  app.ui.line("Unregistered \"" + branch_path + "\"");
  return 0;
}

int cmd_list(App& app, const std::vector<std::string>& args) {
  const std::string target = args.size() > 1 && !starts_with(args[1], "-") ? args[1] : std::string();
  // A leading slash means a branch path, never a filesystem path, so "/" keeps
  // working as "the root branch" instead of resolving to the drive root.
  std::string fs_start;
  if (!target.empty() && target != "/" && !starts_with(target, "/")) {
    const std::string abs = fs::absolute(target);
    if (fs::exists(abs)) fs_start = abs;
  }
  Archive a = open_here(app, fs_start);
  const bool tree = has_flag(args, "-tree");
  const bool releases = has_flag(args, "-releases");
  if (has_flag(args, "-sys")) {
    /* System namespaces only: the branch store and the release channels. */
    app.ui.line("branches/");
    for (const auto& b : a.branches()) {
      if (b.path == "/") continue;
      app.ui.line("  " + b.path + (b.head_seq > 0 ? "  @" + std::to_string(b.head_seq) : ""));
    }
    app.ui.line("releases/tag/v1.0");
    app.ui.line("releases/rawdata/v1.0");
    const auto records = list_registry(a.root());
    if (!records.empty()) {
      app.ui.line("registry/");
      for (const auto& r : records) {
        app.ui.line("  " + short_sha(r.id) + kRegistrySuffix + "  " + r.path);
      }
    }
    return 0;
  }
  int max_depth = -1;
  if (has_flag(args, "-depth")) {
    const std::string v = opt_value(args, "-depth", "-1");
    if (!v.empty()) max_depth = std::atoi(v.c_str());
  }
  // A path argument selects one branch/node: list its files instead of the tree
  // of branches. "/" and "" both mean the archive root branch.
  std::string branch_path = target;
  if (!branch_path.empty() && branch_path != "/") {
    std::string rel = branch_path;
    if (!starts_with(rel, "/")) {
      const std::string abs = fs::absolute(rel);
      rel = abs.size() > a.root().size() ? abs.substr(a.root().size()) : std::string();
      std::replace(rel.begin(), rel.end(), '\\', '/');
      if (rel.empty() || rel.front() != '/') rel = "/" + rel;
    }
    if (a.branch_exists(rel)) branch_path = rel;
    else if (a.branch_exists(target)) branch_path = target;
    else if (!fs_start.empty() && a.branch_exists("/")) branch_path.clear();
  }
  if (!branch_path.empty() && a.branch_exists(branch_path)) {
    print_file_tree(app, a, branch_path, max_depth, has_flag(args, "-line"));
    return 0;
  }
  if (!branch_path.empty() && branch_path != "/") {
    fail(3013, msg("store.no_branch"), branch_path);
  }
  print_branch_tree(app, a, tree, releases, max_depth);
  return 0;
}

/* Content search over the tracked snapshots: every branch head is walked, object
   bodies are scanned line by line and matches are reported as path:line: text. */
int cmd_search(App& app, const std::vector<std::string>& args) {
  const i64 started = now_ms();
  Archive a = open_here(app);
  const std::string pattern = need_arg(args, 1, "<pattern>");
  const std::string path_glob = opt_value(args, "-path", std::string());
  std::string branch_filter = opt_value(args, "-branch", std::string());
  if (branch_filter.empty()) {
    /* Legacy shape "inv search <pattern> <branch>" keeps working next to
       "-branch"; the second positional is the branch when it names one. */
    const std::vector<std::string> pos = positionals(args);
    if (pos.size() > 2 && a.find(pos[2])) branch_filter = pos[2];
  }
  const bool case_sensitive = has_flag(args, "-case");
  const bool use_regex = has_flag(args, "-regex");
  std::size_t limit = 200;
  const std::string limit_text = opt_value(args, "-limit", std::string());
  if (!limit_text.empty()) {
    const long long parsed = std::atoll(limit_text.c_str());
    if (parsed > 0) limit = static_cast<std::size_t>(parsed);
  }

  std::vector<BranchInfo> scope;
  if (!branch_filter.empty()) {
    const auto found = a.find(branch_filter);
    if (!found) fail(3013, msg("store.no_branch"), branch_filter);
    scope.push_back(*found);
  } else {
    scope = a.branches();
  }

  std::regex rx;
  if (use_regex) {
    std::regex_constants::syntax_option_type flags = std::regex::ECMAScript;
    if (!case_sensitive) flags |= std::regex::icase;
    try {
      rx = std::regex(pattern, flags);
    } catch (const std::regex_error& e) {
      fail(3106, msg("search.invalid_regex"), pattern + " (" + e.what() + ")");
    }
  }

  const std::string needle = case_sensitive ? pattern : to_lower(pattern);
  const auto line_hit = [&](const std::string& line) -> bool {
    if (use_regex) return std::regex_search(line, rx);
    if (case_sensitive) return line.find(needle) != std::string::npos;
    return to_lower(line).find(needle) != std::string::npos;
  };

  if (has_flag(args, "-name")) {
    /* Name mode: match the tracked path instead of the file body. */
    std::size_t name_hits = 0;
    bool name_truncated = false;
    for (const auto& b : scope) {
      auto snap = a.head(b.path);
      if (!snap) continue;
      for (const auto& e : snap->entries) {
        if (e.dir) continue;
        if (!path_glob.empty() && !gen::glob_match(path_glob, e.path)) continue;
        if (!line_hit(e.path)) continue;
        if (name_hits >= limit) {
          name_truncated = true;
          break;
        }
        ++name_hits;
        app.ui.line(b.path + "  " + e.path + "  " + human_size(static_cast<i64>(e.size)));
      }
      if (name_truncated) break;
    }
    if (name_hits == 0) {
      app.ui.line(msg("search.none"));
    } else {
      app.ui.line(std::string(msg("search.hits")) + ": " + std::to_string(name_hits) + "  (" +
                  human_duration(now_ms() - started) + ")");
    }
    if (name_truncated) app.ui.warn(msg("search.limit"));
    return 0;
  }

  std::size_t hits = 0;
  std::size_t binary_skipped = 0;
  bool truncated = false;
  for (const auto& b : scope) {
    auto snap = a.head(b.path);
    if (!snap) continue;
    for (const auto& e : snap->entries) {
      if (e.dir) continue;
      if (!path_glob.empty() && !gen::glob_match(path_glob, e.path)) continue;
      std::string data;
      if (!a.objects().get(e.sha, data)) continue;
      if (data.find('\0') != std::string::npos) {
        ++binary_skipped;
        continue;
      }
      std::size_t line_no = 0;
      std::size_t pos = 0;
      while (true) {
        std::size_t end = data.find('\n', pos);
        if (end == std::string::npos) end = data.size();
        std::string line = data.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        ++line_no;
        if (line_hit(line)) {
          if (hits >= limit) {
            truncated = true;
            break;
          }
          ++hits;
          std::string shown = line;
          if (shown.size() > 200) shown = shown.substr(0, 200) + "...";
          app.ui.line(b.path + "  " + e.path + ":" + std::to_string(line_no) + ": " + shown);
        }
        if (end == data.size()) break;
        pos = end + 1;
      }
      if (truncated) break;
    }
    if (truncated) break;
  }

  if (hits == 0) {
    app.ui.line(msg("search.none"));
  } else {
    app.ui.line(std::string(msg("search.hits")) + ": " + std::to_string(hits) + "  (" +
                human_duration(now_ms() - started) + ")");
  }
  if (binary_skipped > 0) {
    app.ui.line(std::string(msg("search.binary_skipped")) + ": " + std::to_string(binary_skipped));
  }
  if (truncated) app.ui.warn(msg("search.limit"));
  return 0;
}

int cmd_rb(App& app, const std::vector<std::string>& args) {
  Archive a = open_here(app);
  const std::string branch_path = need_arg(args, 1, "<path>");
  const std::string seq_text = need_arg(args, 2, "<branch>");
  const i64 seq = std::atoll(seq_text.c_str());
  if (!a.branch_exists(branch_path)) fail(3013, msg("store.no_branch"), branch_path);
  if (!app.yes) {
    app.ui.line(msg("rb.warning"));
    if (!app.ui.prompt(msg("rb.confirm"), "Y/N")) {
      app.ui.line(msg("ui.cancelled"));
      return 1;
    }
  }
  const std::string target = opt_value(args, "-path", a.root());
  a.materialize(branch_path, seq, target, &app.ui);
  app.ui.line(msgf2("rb.done", branch_path, std::to_string(seq)));
  return 0;
}

int cmd_rm(App& app, const std::vector<std::string>& args) {
  Archive a = open_here(app);
  const std::string branch_path = need_arg(args, 1, "<branch>");
  if (!a.branch_exists(branch_path)) fail(3013, msg("store.no_branch"), branch_path);
  if (!app.yes) {
    app.ui.line(msg("rm.warning"));
    if (!app.ui.prompt(msg("rm.confirm"), "Y/N")) {
      app.ui.line(msg("ui.cancelled"));
      return 1;
    }
  }
  a.remove_branch(branch_path, has_flag(args, "-recursive"));
  app.ui.line(msgf("rm.done", branch_path));
  return 0;
}

int cmd_verify(App& app, const std::vector<std::string>& args) {
  Archive a = open_here(app);
  const bool summary_only = has_flag(args, "-summary");
  const auto objs = a.objects().all_objects();
  int bad = 0;
  for (const auto& sha : objs) {
    std::string data;
    if (!a.objects().get(sha, data)) {
      ++bad;
      if (!summary_only) app.ui.warn(std::string(msg("verify.corrupt")) + ": " + sha);
      continue;
    }
    const std::string got = Sha256::hex(data.data(), data.size());
    if (got != sha) {
      ++bad;
      if (!summary_only) app.ui.warn(std::string(msg("verify.mismatch")) + ": " + sha);
    }
  }
  app.ui.line(msgf2("verify.summary", std::to_string(objs.size()), std::to_string(bad)));
  return bad == 0 ? 0 : 1;
}

int cmd_gc(App& app, const std::vector<std::string>& args) {
  Archive a = open_here(app);
  std::set<std::string> live;
  for (const auto& b : a.branches()) {
    for (const auto& s : a.history(b.path)) {
      for (const auto& e : s.entries) {
        if (!e.dir && !e.sha.empty()) live.insert(e.sha);
      }
    }
  }
  const u64 before = a.objects().total_bytes();
  const std::size_t count_before = a.objects().all_objects().size();
  if (has_flag(args, "-dry-run")) {
    /* Report what gc would drop without touching the store. */
    std::size_t dead = 0;
    u64 dead_bytes = 0;
    for (const auto& sha : a.objects().all_objects()) {
      if (live.count(sha) != 0) continue;
      std::string data;
      if (a.objects().get(sha, data)) dead_bytes += data.size();
      ++dead;
    }
    app.ui.line("gc dry run: " + std::to_string(dead) + " " + msg("ui.items") + ", " +
                human_size(static_cast<i64>(dead_bytes)) + " reclaimable");
    app.ui.line("objects " + std::to_string(count_before) + ", live " + std::to_string(live.size()));
    return 0;
  }
  a.objects().gc(live);
  const u64 after = a.objects().total_bytes();
  app.ui.line(msgf2("gc.summary", std::to_string(count_before - live.size()), human_size(static_cast<i64>(before - after))));
  return 0;
}

int cmd_doctor(App& app, const std::vector<std::string>&) {
  const i64 started = now_ms();
  app.ui.line("inv doctor");
  const u64 freeb = fs::free_space(app.cwd);
  app.ui.line(std::string("  cwd        ") + app.cwd);
  app.ui.line(std::string("  home       ") + app.home);
  app.ui.line(std::string("  free disk  ") + human_size(static_cast<i64>(freeb)));
  app.ui.line(std::string("  lang       ") + lang_code(app.lang));
  app.ui.line(std::string("  settings   ") + default_settings_path(app) +
              (fs::exists(default_settings_path(app)) ? " (present)" : " (defaults)"));
  app.ui.line(std::string("  self-tests ") + std::to_string(gen::self_test_count()));
  app.ui.line(std::string("  commands   ") + std::to_string(gen::command_count()));
  app.ui.line(std::string("  errors     ") + std::to_string(gen::error_count()));
  app.ui.line(std::string("  unicode    ") + std::to_string(gen::unicode_table_codepoints()) +
              " codepoints, " + std::to_string(gen::unicode_name_count()) + " names");
  const char* probe_name = gen::unicode_name(0x4E2Du);
  app.ui.line(std::string("  width      U+4E2D=") + (probe_name == nullptr ? "?" : probe_name) +
              " [" + gen::unicode_category(0x4E2Du) + "] cells=" +
              std::to_string(gen::text_width("\xE4\xB8\xAD")));
  const int proto_types = gen::proto_type_count();
  app.ui.line(std::string("  protocol   ") + std::to_string(proto_types) + " types" +
              (proto_types > 0 && gen::proto_type_name(0) != nullptr
                   ? std::string(", first=") + gen::proto_type_name(0)
                   : std::string()));
  app.ui.line(std::string("  transport  ") + transport_backend_name());
  app.ui.line(std::string("  elapsed    ") + human_duration(now_ms() - started));
  return 0;
}

int cmd_self_test(App& app, const std::vector<std::string>& args) {
  const i64 started = now_ms();
  const std::string filter = args.size() > 1 && !starts_with(args[1], "-") ? args[1] : std::string();
  int failures = 0;
  int ran = 0;
  const int total = gen::self_test_count();
  for (int i = 0; i < total; ++i) {
    std::string name, detail;
    const bool failed = gen::self_test_case(i, name, detail);
    if (!filter.empty() && name.find(filter) == std::string::npos) continue;
    ++ran;
    if (failed) {
      ++failures;
      app.ui.warn("FAIL " + name + (detail.empty() ? "" : "  " + detail));
    }
  }
  /* The generated vectors are also executed through the vector runner so the
     aggregate pass/fail counts come from the generated harness itself. */
  if (filter.empty()) {
    int vector_pass = 0;
    int vector_fail = 0;
    const int vector_total = gen::self_test_run(&vector_pass, &vector_fail);
    if (vector_total != total || vector_fail != failures) {
      app.ui.warn("vector runner disagrees: total=" + std::to_string(vector_total) +
                  " pass=" + std::to_string(vector_pass) + " fail=" + std::to_string(vector_fail));
      failures += vector_fail > failures ? (vector_fail - failures) : 0;
    }
    app.ui.line("generated vectors: " + std::to_string(vector_total) + " total, " +
                std::to_string(vector_pass) + " passed, " + std::to_string(vector_fail) + " failed");
  }
  /* Crypto vectors are hand-written invariants that must hold for every build. */
  struct Vec { const char* name; bool ok; };
  const std::string abc = Sha256::hex("abc", 3);
  const std::string empty_sha = Sha256::hex("", 0);
  std::string round;
  const std::string sample = "vanadium";
  const std::string comp = lz4_compress(sample.data(), sample.size());
  const bool lz_ok = lz4_decompress(comp.data(), comp.size(), round, sample.size()) && round == sample;
  std::string b64_out;
  const bool b64_ok = base64_decode(base64_encode(sample), b64_out) && b64_out == sample;
  const Vec vecs[] = {
      {"crypto.sha256_abc", abc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
      {"crypto.sha256_empty", empty_sha == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"crypto.crc64_xz", crc64_hex(crc64("123456789", 9)) == "995dc9bbdf1939fa"},
      {"crypto.adler32", adler32("Wikipedia", 9) == 0x11E60398u},
      {"crypto.lz4_roundtrip", lz_ok},
      {"crypto.base64_roundtrip", b64_ok},
  };
  for (const auto& v : vecs) {
    if (!filter.empty() && std::string(v.name).find(filter) == std::string::npos) continue;
    ++ran;
    if (!v.ok) {
      ++failures;
      app.ui.warn(std::string("FAIL ") + v.name);
    }
  }
  if (!app.quiet) app.ui.line(msgf2("selftest.summary", std::to_string(ran), std::to_string(failures)));
  if (!app.quiet) app.ui.line(std::string(msg("ui.elapsed")) + ": " + human_duration(now_ms() - started));
  return failures;
}

int cmd_bench(App& app, const std::vector<std::string>&) {
  const i64 started = now_ms();
  const std::size_t n = 16u * 1024u * 1024u;
  std::string data;
  data.resize(n);
  for (std::size_t i = 0; i < n; ++i) data[i] = static_cast<char>((i * 31 + 7) & 0xFF);

  i64 t0 = now_ms();
  const std::string sha = Sha256::hex(data.data(), data.size());
  const i64 sha_ms = now_ms() - t0;
  app.ui.line("sha256   16 MiB  " + std::to_string(sha_ms) + " ms  " + short_sha(sha));

  t0 = now_ms();
  const u64 crc = crc64(data.data(), data.size());
  const i64 crc_ms = now_ms() - t0;
  app.ui.line("crc64    16 MiB  " + std::to_string(crc_ms) + " ms  " + crc64_hex(crc));

  const std::size_t m = 4u * 1024u * 1024u;
  std::string small(data.data(), m);
  t0 = now_ms();
  const std::string comp = lz4_compress(small.data(), small.size());
  const i64 comp_ms = now_ms() - t0;
  std::string back;
  t0 = now_ms();
  const bool ok = lz4_decompress(comp.data(), comp.size(), back, small.size());
  const i64 dec_ms = now_ms() - t0;
  app.ui.line("lz4      4 MiB   " + std::to_string(comp_ms) + " ms compress -> " +
              human_size(static_cast<i64>(comp.size())) + ", " + std::to_string(dec_ms) +
              " ms decompress" + (ok && back == small ? "" : "  MISMATCH"));
  app.ui.line(std::string(msg("ui.elapsed")) + ": " + human_duration(now_ms() - started));
  return 0;
}

int cmd_completion(App& app, const std::vector<std::string>& args) {
  const bool ps = has_flag(args, "-powershell") || has_flag(args, "--powershell");
  std::string names;
  for (int i = 0; i < gen::command_count(); ++i) {
    const char* n = gen::command_name(i);
    if (!n) continue;
    if (!names.empty()) names += " ";
    names += n;
  }
  if (ps) {
    app.ui.line("Register-ArgumentCompleter -Native -CommandName inv -ScriptBlock {");
    app.ui.line("  param($wordToComplete, $commandAst, $cursorPosition)");
    app.ui.line("  $commands = @('" + names + "')");
    app.ui.line("  $commands | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {");
    app.ui.line("    [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)");
    app.ui.line("  }");
    app.ui.line("}");
  } else {
    app.ui.line("_inv_complete() {");
    app.ui.line("  local cur=\"${COMP_WORDS[COMP_CWORD]}\"");
    app.ui.line("  COMPREPLY=( $(compgen -W \"" + names + "\" -- \"$cur\") )");
    app.ui.line("}");
    app.ui.line("complete -F _inv_complete inv");
  }
  return 0;
}

/* --------------------------------------------------------------- dispatch */

namespace {

struct CmdEntry {
  const char* name;
  int (*fn)(App&, const std::vector<std::string>&);
};

const CmdEntry kCommands[] = {
    {"help", cmd_help},
    {"version", cmd_version},
    {"set", cmd_set},
    {"init", cmd_init},
    {"status", cmd_status},
    {"push", cmd_push},
    {"pull", cmd_pull},
    {"log", cmd_log},
    {"diff", cmd_diff},
    {"reg", cmd_reg},
    {"unreg", cmd_unreg},
    {"list", cmd_list},
    {"search", cmd_search},
    {"rb", cmd_rb},
    {"rm", cmd_rm},
    {"clone", cmd_clone},
    {"fork", cmd_fork},
    {"login", cmd_login},
    {"release", cmd_release},
    {"edit", cmd_edit},
    {"download", cmd_download},
    {"verify", cmd_verify},
    {"gc", cmd_gc},
    {"doctor", cmd_doctor},
    {"report", cmd_report},
    {"remote", cmd_remote},
    {"self-test", cmd_self_test},
    {"bench", cmd_bench},
    {"completion", cmd_completion},
};

}  // namespace

int dispatch(App& app, const std::vector<std::string>& args) {
  app.cwd = fs::absolute(app.cwd.empty() ? std::string(".") : app.cwd);
  app.home = app.home.empty() ? fs::join(fs::appdata_dir(), "inv") : app.home;
  load_settings(app);
  if (!app.lang_locked) app.lang = language();

  std::vector<std::string> rest;
  rest.reserve(args.size());
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "-lang" || a == "--lang") {
      if (i + 1 < args.size()) {
        app.lang = lang_from_code(args[++i]);
        set_language(app.lang);
      }
      continue;
    }
    if (starts_with(a, "-lang=") || starts_with(a, "--lang=")) {
      app.lang = lang_from_code(a.substr(a.find('=') + 1));
      set_language(app.lang);
      continue;
    }
    if (a == "-json" || a == "--json") {
      app.json = true;
      continue;
    }
    if (a == "-yes" || a == "--yes" || a == "-y") {
      app.yes = true;
      continue;
    }
    if (a == "-quiet" || a == "--quiet") {
      app.quiet = true;
      continue;
    }
    if (a == "-no-color" || a == "--no-color") {
      app.no_color = true;
      continue;
    }
    rest.push_back(a);
  }
  if (rest.size() < 2) {
    print_help(app, std::string());
    return 0;
  }
  const std::string name = rest[1];
  /* Handlers take args[0] as the command name and args[1..] as positionals. */
  std::vector<std::string> sub(rest.begin() + 1, rest.end());
  for (const auto& entry : kCommands) {
    if (name == entry.name) return entry.fn(app, sub);
  }
  Failure f;
  f.code = 1002;
  f.message = std::string(msg("cli.no_command")) + ": " + name;
  throw f;
}

}  // namespace vd
