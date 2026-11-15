#include "vanadium/commands.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/crypto.h"
#include "vanadium/remote.h"
#include "vanadium/store.h"

namespace vd {
namespace {

/* .ivpack container: magic "IVPK1", u32 count, then per entry:
   u8 dir, u32 path_len, u64 data_len, path bytes, data bytes (little endian). */
constexpr const char* kPackMagic = "IVPK1";

struct PackEntry {
  std::string path;
  std::string data;
  bool dir = false;
};

void append_u32(std::string& out, u32 v) { out.append(reinterpret_cast<const char*>(&v), 4); }
void append_u64(std::string& out, u64 v) { out.append(reinterpret_cast<const char*>(&v), 8); }

u32 read_u32(const std::string& s, std::size_t& pos) {
  u32 v = 0;
  if (pos + 4 <= s.size()) std::memcpy(&v, s.data() + pos, 4);
  pos += 4;
  return v;
}

u64 read_u64(const std::string& s, std::size_t& pos) {
  u64 v = 0;
  if (pos + 8 <= s.size()) std::memcpy(&v, s.data() + pos, 8);
  pos += 8;
  return v;
}

void collect_dir(const std::string& base, const std::string& rel, std::vector<PackEntry>& out) {
  const std::string dir = rel.empty() ? base : fs::join(base, rel);
  for (const auto& name : fs::list_dir(dir)) {
    const std::string child_rel = rel.empty() ? name : rel + "/" + name;
    const std::string full = fs::join(base, child_rel);
    if (fs::is_dir(full)) {
      PackEntry e;
      e.path = child_rel;
      e.dir = true;
      out.push_back(e);
      collect_dir(base, child_rel, out);
    } else if (fs::is_file(full)) {
      PackEntry e;
      e.path = child_rel;
      e.data = fs::read_file(full);
      out.push_back(e);
    }
  }
}

std::string pack_directory(const std::string& root) {
  std::vector<PackEntry> entries;
  collect_dir(root, "", entries);
  std::string out = kPackMagic;
  append_u32(out, static_cast<u32>(entries.size()));
  for (const auto& e : entries) {
    out.push_back(e.dir ? 1 : 0);
    append_u32(out, static_cast<u32>(e.path.size()));
    append_u64(out, static_cast<u64>(e.data.size()));
    out += e.path;
    out += e.data;
  }
  return out;
}

bool unpack_directory(const std::string& blob, const std::string& target, std::string& why) {
  if (blob.size() < 9 || blob.compare(0, 5, kPackMagic) != 0) {
    why = "bad pack magic";
    return false;
  }
  std::size_t pos = 5;
  const u32 count = read_u32(blob, pos);
  fs::mkdirs(target);
  for (u32 i = 0; i < count; ++i) {
    if (pos >= blob.size()) {
      why = "truncated pack header";
      return false;
    }
    const bool is_dir = blob[pos++] != 0;
    const u32 path_len = read_u32(blob, pos);
    const u64 data_len = read_u64(blob, pos);
    if (pos + path_len + data_len > blob.size()) {
      why = "truncated pack body";
      return false;
    }
    const std::string rel = blob.substr(pos, path_len);
    pos += path_len;
    const std::string dest = fs::join(target, rel);
    if (is_dir) {
      fs::mkdirs(dest);
    } else {
      fs::mkdirs(fs::parent(dest));
      fs::write_file_atomic(dest, blob.substr(pos, static_cast<std::size_t>(data_len)));
    }
    pos += static_cast<std::size_t>(data_len);
  }
  return true;
}

remote::Github github_for(App& app, const std::string& host) {
  std::string token = remote::load_token(host);
  if (token.empty()) {
    Failure f;
    f.code = 4001;
    f.message = msg("login.needed");
    f.detail = host;
    throw f;
  }
  return remote::Github(token);
}

std::string require_repo(App& app, const std::vector<std::string>& args, std::string& owner,
                         std::string& repo) {
  const std::string url = need_arg(args, 1, "<url>");
  if (!remote::parse_repo_url(url, owner, repo)) {
    Failure f;
    f.code = 4002;
    f.message = msg("remote.bad_url");
    f.detail = url;
    throw f;
  }
  return url;
}

void load_settings(App& app) {
  Settings s;
  s.load(fs::join(app.home, "settings.json"));
  app.settings = s;
}

Archive open_here(App& app) {
  std::string dir = app.cwd;
  for (int i = 0; i < 64; ++i) {
    if (Archive::looks_like_archive(dir)) return Archive::open(dir);
    const std::string up = fs::parent(dir);
    if (up == dir || up.empty()) break;
    dir = up;
  }
  Failure f;
  f.code = 3001;
  f.message = msg("archive.not_found");
  f.detail = app.cwd;
  throw f;
}

int threads_from(const std::vector<std::string>& args) {
  const std::string t = opt_value(args, "-thread", "8");
  int n = std::atoi(t.c_str());
  if (n < 1) n = 1;
  if (n > 16) n = 16;
  return n;
}

}  // namespace

int cmd_login(App& app, const std::vector<std::string>& args) {
  const std::string url = need_arg(args, 1, "<url>");
  std::string host = "github.com";
  if (starts_with(url, "http")) {
    std::string rest = url.substr(url.find("://") + 3);
    const std::size_t slash = rest.find('/');
    host = slash == std::string::npos ? rest : rest.substr(0, slash);
  }
  std::string token = opt_value(args, "-token", std::string());
  const std::string name = opt_value(args, "-name", std::string());
  if (token.empty()) {
    app.ui.line(msgf("login.token_hint", host));
    if (!app.ui.prompt(msg("login.token_prompt"), "token")) return 1;
    token = opt_value(args, "-token", std::string());
    if (token.empty()) {
      app.ui.warn(msg("login.no_token"));
      return 1;
    }
  }
  remote::save_token(host, name, token);
  remote::Github gh(token);
  Json me = gh.user();
  const std::string login = me.contains("login") ? me["login"].as_string() : std::string("?");
  app.ui.line(msgf2("login.saved", host, login));
  return 0;
}

int cmd_download(App& app, const std::vector<std::string>& args) {
  const i64 started = now_ms();
  const std::string url = need_arg(args, 1, "<url>");
  std::string out_path = opt_value(args, "-path", std::string());
  if (out_path.empty()) {
    std::string clean = url;
    const std::size_t q = clean.find('?');
    if (q != std::string::npos) clean = clean.substr(0, q);
    out_path = fs::filename(clean);
    if (out_path.empty()) out_path = "download.bin";
    out_path = fs::join(app.cwd, out_path);
  }
  remote::DownloadReport rep;
  const int threads = threads_from(args);
  app.ui.status("download", msgf("download.starting", url));
  const bool ok = remote::download_segmented(url, out_path, threads, &app.ui, &rep);
  app.ui.clear_status();
  if (!ok) {
    app.ui.error(4003, msgf("download.failed", url));
    return 1;
  }
  const std::string data = fs::read_file(out_path);
  const std::string sha = Sha256::hex(data.data(), data.size());
  const u64 crc = crc64(data.data(), data.size());
  app.ui.line("sha256 " + sha);
  app.ui.line("crc64  " + crc64_hex(crc));
  app.ui.line("size   " + human_size(static_cast<i64>(data.size())));
  const std::string want_sha = opt_value(args, "-sha256", std::string());
  const std::string want_crc = opt_value(args, "-crc64", std::string());
  bool verified = true;
  if (!want_sha.empty() && to_lower(want_sha) != sha) {
    app.ui.error(4004, msg("download.sha_mismatch"));
    verified = false;
  }
  if (!want_crc.empty() && to_lower(want_crc) != crc64_hex(crc)) {
    app.ui.error(4005, msg("download.crc_mismatch"));
    verified = false;
  }
  app.ui.line(msgf2("download.done", human_size(static_cast<i64>(data.size())),
                    human_duration(now_ms() - started)));
  return verified ? 0 : 1;
}

int cmd_remote(App& app, const std::vector<std::string>&) {
  load_settings(app);
  bool any = false;
  for (const auto& kv : app.settings.raw()) {
    if (!starts_with(kv.first, "url_")) continue;
    any = true;
    app.ui.line(kv.first.substr(4) + "  " + kv.second);
  }
  if (!any) app.ui.line(msg("remote.none"));
  return 0;
}

int cmd_push(App& app, const std::vector<std::string>& args) {
  const i64 started = now_ms();
  std::string owner, repo;
  require_repo(app, args, owner, repo);
  Archive a = open_here(app);
  remote::Github gh = github_for(app, "github.com");

  Json rel;
  try {
    rel = gh.get_release(owner, repo, "inv-archive");
  } catch (const Failure&) {
    rel = gh.create_release(owner, repo, "inv-archive", "inv archive", "Vanadium archive packs", true);
  }
  if (!rel.contains("upload_url")) {
    app.ui.error(4006, msg("push.no_release"));
    return 1;
  }
  const std::string upload_url = rel["upload_url"].as_string();
  const std::string pack = pack_directory(a.root());
  const std::string tmp = fs::join(fs::temp_dir(), "inv_" + random_hex(6) + ".ivpack");
  fs::write_file(tmp, pack);
  const std::string asset_name = "inv-" + a.name() + "-" + std::to_string(a.objects().all_objects().size()) + "-" +
                                 std::to_string(now_ms()) + ".ivpack";
  Json asset = gh.upload_asset(upload_url, tmp, asset_name);
  fs::remove_file(tmp);
  const std::string state = asset.contains("state") ? asset["state"].as_string() : "?";
  app.ui.line(msgf2("push.uploaded", asset_name, state));
  app.ui.line(msgf2("push.summary", std::to_string(a.branches().size()),
                    human_duration(now_ms() - started)));
  return 0;
}

int cmd_pull(App& app, const std::vector<std::string>& args) {
  const i64 started = now_ms();
  std::string owner, repo;
  require_repo(app, args, owner, repo);
  remote::Github gh = github_for(app, "github.com");
  Json rel = gh.get_release(owner, repo, "inv-archive");
  if (!rel.contains("assets") || rel["assets"].size() == 0) {
    app.ui.error(4007, msg("pull.no_assets"));
    return 1;
  }
  const std::string tag_filter = opt_value(args, "-tag", std::string());
  const Json& assets = rel["assets"];
  std::string url;
  std::string name;
  for (std::size_t i = assets.size(); i-- > 0;) {
    const Json& as = assets[i];
    const std::string an = as.contains("name") ? as["name"].as_string() : std::string();
    if (!ends_with(an, ".ivpack")) continue;
    if (!tag_filter.empty() && an.find(tag_filter) == std::string::npos) continue;
    name = an;
    url = as["browser_download_url"].as_string();
    break;
  }
  if (url.empty()) {
    app.ui.error(4008, msg("pull.no_pack"));
    return 1;
  }
  const std::string tmp = fs::join(fs::temp_dir(), "inv_pull_" + random_hex(6) + ".ivpack");
  app.ui.status("pull", msgf("pull.fetching", name));
  const bool ok = remote::download_segmented(url, tmp, threads_from(args), &app.ui, nullptr);
  app.ui.clear_status();
  if (!ok) {
    app.ui.error(4009, msg("pull.download_failed"));
    return 1;
  }
  Archive a = open_here(app);
  const std::string blob = fs::read_file(tmp);
  fs::remove_file(tmp);
  std::string why;
  if (!unpack_directory(blob, a.root(), why)) {
    app.ui.error(4010, std::string(msg("pull.bad_pack")) + ": " + why);
    return 1;
  }
  a.reload();
  app.ui.line(msgf2("pull.done", std::to_string(a.branches().size()), human_duration(now_ms() - started)));
  return 0;
}

int cmd_clone(App& app, const std::vector<std::string>& args) {
  const i64 started = now_ms();
  std::string owner, repo;
  const std::string url = require_repo(app, args, owner, repo);
  std::string target = opt_value(args, "-path", std::string());
  if (target.empty()) target = fs::join(app.cwd, repo);
  target = fs::absolute(target);
  if (Archive::looks_like_archive(target)) {
    app.ui.warn(msgf("clone.exists", target));
    return 1;
  }
  remote::Github gh = github_for(app, "github.com");
  Json rel = gh.get_release(owner, repo, "inv-archive");
  if (!rel.contains("assets") || rel["assets"].size() == 0) {
    app.ui.error(4011, msg("clone.no_pack"));
    return 1;
  }
  const Json& assets = rel["assets"];
  const std::string url_pack = assets[assets.size() - 1]["browser_download_url"].as_string();
  const std::string tmp = fs::join(fs::temp_dir(), "inv_clone_" + random_hex(6) + ".ivpack");
  app.ui.status("clone", msgf("clone.fetching", owner + "/" + repo));
  const bool ok = remote::download_segmented(url_pack, tmp, threads_from(args), &app.ui, nullptr);
  app.ui.clear_status();
  if (!ok) {
    app.ui.error(4012, msg("clone.download_failed"));
    return 1;
  }
  const std::string blob = fs::read_file(tmp);
  fs::remove_file(tmp);
  std::string why;
  if (!unpack_directory(blob, target, why)) {
    app.ui.error(4013, std::string(msg("clone.bad_pack")) + ": " + why);
    return 1;
  }
  Archive a = Archive::open(target);
  app.ui.line(msgf("clone.done", a.root()));
  app.ui.line(msgf2("clone.summary", std::to_string(a.branches().size()), human_duration(now_ms() - started)));
  (void)url;
  return 0;
}

int cmd_fork(App& app, const std::vector<std::string>& args) {
  std::string owner, repo;
  require_repo(app, args, owner, repo);
  std::string path = opt_value(args, "-path", std::string());
  if (path.empty()) path = fs::join(app.cwd, opt_value(args, "-name", repo));
  const std::string name = opt_value(args, "-name", repo);
  std::vector<std::string> clone_args = {"inv", "clone", args[1], "-path", path};
  const int rc = cmd_clone(app, clone_args);
  if (rc != 0) return rc;
  Archive a = Archive::open(path);
  Json meta = Json::object();
  meta.set("version", Json(1));
  meta.set("name", Json(name));
  meta.set("type", Json(std::string(archive_type_name(a.type()))));
  fs::write_file(fs::join(a.root(), "meta.json"), meta.dump(2) + "\n");
  app.ui.line(msgf("fork.done", name));
  return 0;
}

int cmd_release(App& app, const std::vector<std::string>& args) {
  std::string owner, repo;
  require_repo(app, args, owner, repo);
  remote::Github gh = github_for(app, "github.com");
  const std::string tag = opt_value(args, "-tag", "v1.0");
  const std::string title = opt_value(args, "-title", tag);
  const std::string branch = opt_value(args, "-branch", "/");
  Archive a = open_here(app);
  auto h = a.head(branch);
  const std::string body = h ? ("snapshot @" + std::to_string(h->seq) + " " + h->id) : "empty branch";
  if (has_flag(args, "-relist")) {
    Json list = gh.list_releases(owner, repo);
    app.ui.line(list.dump(2));
    return 0;
  }
  Json rel;
  if (has_flag(args, "-reintro")) {
    rel = gh.update_release(owner, repo, 0, Json::object());
  } else {
    rel = gh.create_release(owner, repo, tag, title, body, false);
  }
  const std::string id = rel.contains("id") ? std::to_string(rel["id"].as_int()) : "?";
  app.ui.line(msgf2("release.done", tag, id));
  return 0;
}

int cmd_edit(App& app, const std::vector<std::string>& args) {
  const std::string type = need_arg(args, 1, "<type>");
  const std::string url = need_arg(args, 2, "<url>");
  load_settings(app);
  if (type == "url") {
    const std::string name = opt_value(args, "-name", std::string());
    if (name.empty()) {
      app.ui.warn(msg("edit.need_name"));
      return 1;
    }
    app.settings.set("url_" + name, url);
    app.settings.save(fs::join(app.home, "settings.json"));
    app.ui.line(msgf2("edit.saved", "url_" + name, url));
    return 0;
  }
  if (type == "token") {
    std::string owner, repo;
    std::string host = "github.com";
    if (remote::parse_repo_url(url, owner, repo)) {
      remote::save_token(host, owner, opt_value(args, "-token", std::string()));
      app.ui.line(msgf("edit.saved", "token"));
      return 0;
    }
    app.ui.error(4014, msg("remote.bad_url"));
    return 1;
  }
  app.ui.warn(std::string(msg("edit.unknown_type")) + ": " + type);
  return 1;
}

int cmd_report(App& app, const std::vector<std::string>& args) {
  Archive a = open_here(app);
  Json out = Json::object();
  out.set("archive", Json(a.name()));
  out.set("root", Json(a.root()));
  out.set("type", Json(std::string(archive_type_name(a.type()))));
  out.set("objects", Json(static_cast<i64>(a.objects().all_objects().size())));
  out.set("bytes", Json(static_cast<i64>(a.objects().total_bytes())));
  Json branches = Json::array();
  for (const auto& b : a.branches()) {
    Json j = Json::object();
    j.set("path", Json(b.path));
    j.set("head", Json(b.head_seq));
    j.set("children", Json(static_cast<i64>(b.children.size())));
    branches.push(j);
  }
  out.set("branches", branches);
  out.set("generated", Json(iso_time(now_ms())));
  const std::string path = opt_value(args, "-out", fs::join(app.cwd, "inv-report.json"));
  fs::write_file_atomic(path, out.dump(2) + "\n");
  app.ui.line(msgf("report.written", path));
  app.ui.summary(out.dump(-1));
  return 0;
}

}  // namespace vd
