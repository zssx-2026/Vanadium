#include "vanadium/commands.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/crypto.h"
#include "vanadium/gen.h"
#include "vanadium/store.h"
#include "vanadium/vd.h"

namespace vd {
namespace {

const char* kVersion = "0.1.0";

/* Walk up from start looking for an archive root (invar.json). */
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

std::string tree_prefix(const std::vector<bool>& last) {
  std::string out;
  for (std::size_t i = 0; i < last.size(); ++i) out += last[i] ? "    " : "|   ";
  return out;
}

void print_branch_tree(App& app, const Archive& a) {
  app.ui.line("/");
  app.ui.line("branches/");
  std::vector<BranchInfo> all = a.branches();
  std::sort(all.begin(), all.end(), [](const BranchInfo& x, const BranchInfo& y) {
    return x.path < y.path;
  });
  for (const auto& b : all) {
    if (b.path == "/") continue;
    const std::string rel = b.path.substr(1);
    const std::size_t depth = static_cast<std::size_t>(std::count(rel.begin(), rel.end(), '/'));
    std::string line = "  ";
    for (std::size_t i = 0; i < depth; ++i) line += "|   ";
    line += "|-- " + b.name;
    if (b.head_seq > 0) line += "  @" + std::to_string(b.head_seq);
    app.ui.line(line);
  }
  app.ui.line("releases/tag/v1.0");
  app.ui.line("releases/rawdata/v1.0");
}

std::string short_sha(const std::string& sha, std::size_t n = 12) {
  return sha.size() <= n ? sha : sha.substr(0, n);
}

}  // namespace

/* ---------------------------------------------------------------- helpers */

std::string need_arg(const std::vector<std::string>& args, std::size_t i, const char* name) {
  if (i >= args.size() || args[i].empty()) {
    Failure f;
    f.code = 1001;
    f.message = std::string(msg("cli.missing_arg")) + ": " + name;
    throw f;
  }
  return args[i];
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
  if (args.size() < 2) {
    app.ui.line(msg("set.list"));
    for (const auto& d : Settings::defs()) {
      if (d.dynamic) {
        app.ui.line("  " + d.key + "<name> = <url>   (dynamic)");
        continue;
      }
      std::string line = "  " + d.key;
      while (line.size() < 30) line.push_back(' ');
      line += "= " + app.settings.get(d.key);
      app.ui.line(line);
    }
    return 0;
  }
  const std::string key = args[1];
  if (args.size() < 3) {
    app.ui.line("  " + key + " = " + app.settings.get(key));
    return 0;
  }
  app.settings.set(key, args[2]);
  save_settings(app);
  app.ui.line(msgf2("set.saved", key, args[2]));
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
  app.ui.line(msgf("init.created", a.root()));
  done_line(app, "init", a.branches().size(), started);
  return 0;
}

int cmd_status(App& app, const std::vector<std::string>& args) {
  Archive a = open_here(app, args.size() > 1 ? args[1] : std::string());
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
  if (has_flag(args, "-n")) {
    const std::string v = opt_value(args, "-n", "20");
    limit = static_cast<std::size_t>(std::atoll(v.c_str()));
  }
  auto hist = a.history(branch, limit);
  if (hist.empty()) {
    app.ui.line(msg("log.empty"));
    return 0;
  }
  for (auto it = hist.rbegin(); it != hist.rend(); ++it) {
    app.ui.line("@" + std::to_string(it->seq) + "  " + short_sha(it->id) + "  " +
                iso_time(it->time_ms) + "  " + it->message);
  }
  return 0;
}

int cmd_diff(App& app, const std::vector<std::string>&) {
  Archive a = open_here(app);
  std::vector<std::string> deleted;
  auto changed = a.scan_working_tree(a.root(), &deleted);
  if (changed.empty() && deleted.empty()) {
    app.ui.line(msg("ui.clean"));
    return 0;
  }
  for (const auto& e : changed) {
    app.ui.line(std::string(e.dir ? "A  " : "M  ") + e.path + "  " + human_size(static_cast<i64>(e.size)));
  }
  for (const auto& d : deleted) app.ui.line("D  " + d);
  return 0;
}

int cmd_reg(App& app, const std::vector<std::string>& args) {
  const i64 started = now_ms();
  Archive a = open_here(app);
  const std::string raw = need_arg(args, 1, "<path>");
  const std::string abs = fs::absolute(raw);
  if (!fs::is_dir(abs)) fail(3101, msg("store.no_dir"), abs);

  std::string branch_path = opt_value(args, "-branch", std::string());
  if (branch_path.empty()) {
    const std::string rel = abs.substr(a.root().size());
    branch_path = rel.empty() ? "/" : "/" + trim(rel);
    std::replace(branch_path.begin(), branch_path.end(), '\\', '/');
  }
  const ArchiveType type = archive_type_parse(opt_value(args, "-type", archive_type_name(a.type())));
  if (!a.branch_exists(branch_path)) a.create_branch(branch_path, "");

  std::vector<std::string> deleted;
  auto entries = a.scan_working_tree(abs, &deleted);
  for (const auto& e : entries) {
    if (e.dir) continue;
    std::string data = fs::read_file(fs::join(abs, e.path));
    a.objects().put(data);
  }
  const std::string id = a.commit(branch_path, entries, opt_value(args, "-message", "register"));
  app.ui.line(msgf2("reg.committed", branch_path, short_sha(id)));
  done_line(app, "reg", entries.size(), started);
  (void)type;
  return 0;
}

int cmd_unreg(App& app, const std::vector<std::string>& args) {
  Archive a = open_here(app);
  const std::string raw = need_arg(args, 1, "<path>");
  std::string branch_path = raw;
  if (fs::is_dir(fs::absolute(raw)) || fs::is_file(fs::absolute(raw))) {
    const std::string rel = fs::absolute(raw).substr(a.root().size());
    branch_path = rel.empty() ? "/" : "/" + trim(rel);
    std::replace(branch_path.begin(), branch_path.end(), '\\', '/');
  }
  a.remove_branch(branch_path, has_flag(args, "-recursive"));
  app.ui.line(msgf("unreg.done", branch_path));
  return 0;
}

int cmd_list(App& app, const std::vector<std::string>& args) {
  Archive a = open_here(app, args.size() > 1 && !starts_with(args[1], "-") ? args[1] : std::string());
  print_branch_tree(app, a);
  return 0;
}

int cmd_search(App& app, const std::vector<std::string>& args) {
  Archive a = open_here(app, args.size() > 1 && !starts_with(args[1], "-") ? args[1] : std::string());
  const std::string needle = to_lower(need_arg(args, 2, "<branch>"));
  int hits = 0;
  for (const auto& b : a.branches()) {
    const std::string hay = to_lower(b.name + " " + b.path);
    if (hay.find(needle) == std::string::npos) continue;
    ++hits;
    app.ui.line(b.path + "  @" + std::to_string(b.head_seq) + "  " + b.id);
  }
  if (hits == 0) app.ui.line(msg("search.none"));
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

int cmd_verify(App& app, const std::vector<std::string>&) {
  Archive a = open_here(app);
  const auto objs = a.objects().all_objects();
  int bad = 0;
  for (const auto& sha : objs) {
    std::string data;
    if (!a.objects().get(sha, data)) {
      ++bad;
      app.ui.warn(std::string(msg("verify.corrupt")) + ": " + sha);
      continue;
    }
    const std::string got = Sha256::hex(data.data(), data.size());
    if (got != sha) {
      ++bad;
      app.ui.warn(std::string(msg("verify.mismatch")) + ": " + sha);
    }
  }
  app.ui.line(msgf2("verify.summary", std::to_string(objs.size()), std::to_string(bad)));
  return bad == 0 ? 0 : 1;
}

int cmd_gc(App& app, const std::vector<std::string>&) {
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
  app.ui.line(std::string("  unicode    ") + std::to_string(gen::unicode_table_codepoints()));
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
