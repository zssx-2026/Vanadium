#include "vanadium/store.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/crypto.h"
#include "vanadium/i18n.h"
#include "vanadium/ui.h"

namespace vd {
namespace {

constexpr const char* kMetaFile = "meta.json";
/* v0.1.0 archives were written as invar.json; keep reading them. */
constexpr const char* kLegacyMetaFile = "invar.json";
constexpr const char* kObjectsDir = "objects";
constexpr const char* kBranchesDir = "branches";
constexpr const char* kHistoryFile = "history.jsonl";
constexpr const char* kHeadFile = "head.json";
constexpr u32 kObjectMagic = 0x315A5649u; /* "IVZ1" little endian */
constexpr u32 kObjectMagicRaw = 0x305A5649u; /* "IVZ0" little endian: stored raw */

std::string canonical_branch(const std::string& raw) {
  std::string s = trim(raw);
  if (s.empty() || s == "/" || s == "\\" || s == ".") return "/";
  std::replace(s.begin(), s.end(), '\\', '/');
  while (!s.empty() && s.front() == '/') s.erase(s.begin());
  while (!s.empty() && s.back() == '/') s.pop_back();
  if (s.empty()) return "/";
  return "/" + s;
}

std::string branch_name_from_path(const std::string& path) {
  if (path == "/") return "root";
  const std::size_t pos = path.find_last_of('/');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

Json entry_to_json(const Entry& e) {
  Json j = Json::object();
  j.set("path", Json(e.path));
  j.set("dir", Json(e.dir));
  j.set("size", Json(static_cast<i64>(e.size)));
  j.set("sha", Json(e.sha));
  j.set("crc", Json(static_cast<i64>(e.crc)));
  j.set("mtime", Json(e.mtime));
  j.set("mode", Json(static_cast<i64>(e.mode)));
  return j;
}

Entry entry_from_json(const Json& j) {
  Entry e;
  if (j.contains("path")) e.path = j["path"].as_string();
  if (j.contains("dir")) e.dir = j["dir"].as_bool();
  if (j.contains("size")) e.size = static_cast<u64>(j["size"].as_int());
  if (j.contains("sha")) e.sha = j["sha"].as_string();
  if (j.contains("crc")) e.crc = static_cast<u64>(j["crc"].as_int());
  if (j.contains("mtime")) e.mtime = j["mtime"].as_int();
  if (j.contains("mode")) e.mode = static_cast<u32>(j["mode"].as_int(0644));
  return e;
}

Json snapshot_body(const Snapshot& s) {
  Json j = Json::object();
  j.set("seq", Json(s.seq));
  j.set("branch", Json(s.branch));
  j.set("time", Json(s.time_ms));
  j.set("message", Json(s.message));
  j.set("parent", Json(s.parent_sha));
  Json arr = Json::array();
  for (const auto& e : s.entries) arr.push(entry_to_json(e));
  j.set("entries", arr);
  return j;
}

std::string snapshot_id(const Snapshot& s) {
  const std::string body = snapshot_body(s).dump(-1);
  return Sha256::hex(body.data(), body.size());
}

Snapshot snapshot_from_json(const Json& j) {
  Snapshot s;
  if (j.contains("seq")) s.seq = j["seq"].as_int();
  if (j.contains("branch")) s.branch = j["branch"].as_string();
  if (j.contains("time")) s.time_ms = j["time"].as_int();
  if (j.contains("message")) s.message = j["message"].as_string();
  if (j.contains("parent")) s.parent_sha = j["parent"].as_string();
  if (j.contains("id")) s.id = j["id"].as_string();
  if (j.contains("entries") && j["entries"].is_array()) {
    const Json& arr = j["entries"];
    s.entries.reserve(arr.size());
    for (std::size_t i = 0; i < arr.size(); ++i) s.entries.push_back(entry_from_json(arr[i]));
  }
  if (s.id.empty()) s.id = snapshot_id(s);
  return s;
}

Json branch_to_json(const BranchInfo& b) {
  Json j = Json::object();
  j.set("id", Json(b.id));
  j.set("path", Json(b.path));
  j.set("name", Json(b.name));
  j.set("parent", Json(b.parent));
  j.set("created", Json(b.created_ms));
  j.set("head_seq", Json(b.head_seq));
  j.set("is_node", Json(b.is_node));
  j.set("system", Json(b.system));
  Json kids = Json::array();
  for (const auto& c : b.children) kids.push(Json(c));
  j.set("children", kids);
  return j;
}

BranchInfo branch_from_json(const Json& j) {
  BranchInfo b;
  if (j.contains("id")) b.id = j["id"].as_string();
  if (j.contains("path")) b.path = canonical_branch(j["path"].as_string());
  if (j.contains("name")) b.name = j["name"].as_string();
  if (j.contains("parent")) b.parent = j["parent"].as_string();
  if (j.contains("created")) b.created_ms = j["created"].as_int();
  if (j.contains("head_seq")) b.head_seq = j["head_seq"].as_int();
  if (j.contains("is_node")) b.is_node = j["is_node"].as_bool();
  if (j.contains("system")) b.system = j["system"].as_bool();
  if (j.contains("children") && j["children"].is_array()) {
    const Json& arr = j["children"];
    for (std::size_t i = 0; i < arr.size(); ++i) b.children.push_back(arr[i].as_string());
  }
  if (b.name.empty()) b.name = branch_name_from_path(b.path);
  return b;
}

std::string encode_object(const std::string& raw, int level) {
  /* level 0 stores the bytes verbatim ("IVZ0" header) when that is not larger
     than the LZ4 block; level >= 1 forces LZ4 ("IVZ1"). Both decode paths are
     always available, so old and new objects interoperate. */
  const std::string comp = lz4_compress(raw.data(), raw.size());
  const bool use_raw = level <= 0 && raw.size() + 12 <= comp.size() + 12;
  std::string out;
  out.reserve((use_raw ? raw.size() : comp.size()) + 12);
  const u32 magic = use_raw ? kObjectMagicRaw : kObjectMagic;
  const u32 raw_len = static_cast<u32>(raw.size());
  const u32 comp_len = static_cast<u32>(use_raw ? raw.size() : comp.size());
  out.append(reinterpret_cast<const char*>(&magic), 4);
  out.append(reinterpret_cast<const char*>(&raw_len), 4);
  out.append(reinterpret_cast<const char*>(&comp_len), 4);
  out += use_raw ? raw : comp;
  return out;
}

bool decode_object(const std::string& blob, std::string& out) {
  if (blob.size() < 12) return false;
  u32 magic = 0, raw_len = 0, comp_len = 0;
  std::memcpy(&magic, blob.data(), 4);
  std::memcpy(&raw_len, blob.data() + 4, 4);
  std::memcpy(&comp_len, blob.data() + 8, 4);
  if (magic == kObjectMagicRaw) {
    if (comp_len != raw_len || blob.size() < 12 + comp_len) return false;
    out.assign(blob.data() + 12, comp_len);
    return true;
  }
  if (magic != kObjectMagic) return false;
  if (blob.size() < 12 + comp_len) return false;
  return lz4_decompress(blob.data() + 12, comp_len, out, raw_len);
}

}  // namespace

const char* archive_type_name(ArchiveType t) {
  switch (t) {
    case ArchiveType::Full: return "full";
    case ArchiveType::Least: return "least";
    case ArchiveType::NodeOnly:
    default: return "nodeonly";
  }
}

ArchiveType archive_type_parse(const std::string& s) {
  const std::string v = to_lower(trim(s));
  if (v == "full" || v == "f") return ArchiveType::Full;
  if (v == "least" || v == "l") return ArchiveType::Least;
  return ArchiveType::NodeOnly;
}

/* -------------------------------------------------------------- ObjectStore */

ObjectStore::ObjectStore(std::string root) : root_(std::move(root)) {}

std::string ObjectStore::object_path(const std::string& sha) const {
  if (sha.size() < 4) return fs::join(fs::join(root_, kObjectsDir), sha);
  const std::string a = sha.substr(0, 2);
  const std::string b = sha.substr(2, 2);
  return fs::join(fs::join(fs::join(root_, kObjectsDir), a), fs::join(b, sha));
}

std::string ObjectStore::put(const std::string& data) {
  const std::string sha = Sha256::hex(data.data(), data.size());
  const std::string path = object_path(sha);
  if (fs::exists(path)) return sha;
  fs::mkdirs(fs::parent(path));
  fs::write_file_atomic(path, encode_object(data, compress_level_));
  return sha;
}

std::string ObjectStore::put_file(const std::string& path, u64* size_out) {
  std::string data = fs::read_file(path);
  if (size_out) *size_out = static_cast<u64>(data.size());
  return put(data);
}

bool ObjectStore::get(const std::string& sha, std::string& out) const {
  std::string blob;
  if (!fs::read_file_opt(object_path(sha), blob)) return false;
  return decode_object(blob, out);
}

bool ObjectStore::has(const std::string& sha) const { return fs::is_file(object_path(sha)); }

u64 ObjectStore::stored_size(const std::string& sha) const {
  return static_cast<u64>(fs::size(object_path(sha)));
}

std::vector<std::string> ObjectStore::all_objects() const {
  std::vector<std::string> out;
  const std::string base = fs::join(root_, kObjectsDir);
  if (!fs::is_dir(base)) return out;
  for (const auto& a : fs::list_dir(base)) {
    const std::string pa = fs::join(base, a);
    if (!fs::is_dir(pa)) continue;
    for (const auto& b : fs::list_dir(pa)) {
      const std::string pb = fs::join(pa, b);
      if (!fs::is_dir(pb)) continue;
      for (const auto& f : fs::list_dir(pb)) out.push_back(f);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

void ObjectStore::erase(const std::string& sha) {
  const std::string path = object_path(sha);
  if (fs::is_file(path)) fs::remove_file(path);
}

void ObjectStore::gc(const std::set<std::string>& live) {
  for (const auto& sha : all_objects()) {
    if (live.find(sha) == live.end()) erase(sha);
  }
}

u64 ObjectStore::total_bytes() const {
  u64 total = 0;
  for (const auto& sha : all_objects()) total += stored_size(sha);
  return total;
}

/* ------------------------------------------------------------------ Archive */

Archive Archive::create(const std::string& root, const std::string& name, ArchiveType type) {
  Archive a;
  a.root_ = fs::absolute(root);
  a.name_ = name.empty() ? fs::filename(a.root_) : name;
  a.type_ = type;
  a.created_ms_ = now_ms();
  fs::mkdirs(a.root_);
  fs::mkdirs(fs::join(a.root_, kObjectsDir));
  fs::mkdirs(fs::join(a.root_, kBranchesDir));
  a.archive_id_ = random_hex(16);
  a.objects_ = ObjectStore(a.root_);

  BranchInfo root_branch;
  root_branch.id = Sha256::hex("/", 1).substr(0, 12);
  root_branch.path = "/";
  root_branch.name = "root";
  root_branch.parent = "";
  root_branch.created_ms = a.created_ms_;
  root_branch.head_seq = 0;
  root_branch.is_node = true;
  root_branch.system = true;
  a.branches_.push_back(root_branch);
  fs::mkdirs(a.branch_dir(root_branch.id));
  a.save_meta();
  return a;
}

bool Archive::looks_like_archive(const std::string& root) {
  if (!fs::is_dir(root)) return false;
  if (!fs::is_file(fs::join(root, kMetaFile)) && !fs::is_file(fs::join(root, kLegacyMetaFile))) return false;
  return fs::is_dir(fs::join(root, kObjectsDir)) || fs::is_dir(fs::join(root, kBranchesDir));
}

Archive Archive::open(const std::string& root) {
  Archive a;
  a.root_ = fs::absolute(root);
  a.objects_ = ObjectStore(a.root_);
  std::string text;
  if (!fs::read_file_opt(fs::join(a.root_, kMetaFile), text) &&
      !fs::read_file_opt(fs::join(a.root_, kLegacyMetaFile), text)) {
    fail(3001, msg("store.not_archive"), a.root_);
  }
  auto parsed = Json::try_parse(text);
  if (!parsed || !parsed->is_object()) fail(3002, msg("store.bad_meta"), a.root_);
  const Json& doc = *parsed;
  if (doc.contains("name")) a.name_ = doc["name"].as_string();
  if (a.name_.empty()) a.name_ = fs::filename(a.root_);
  if (doc.contains("type")) a.type_ = archive_type_parse(doc["type"].as_string());
  if (doc.contains("id")) a.archive_id_ = doc["id"].as_string();
  if (a.archive_id_.empty()) a.archive_id_ = random_hex(16);
  if (doc.contains("created")) a.created_ms_ = doc["created"].as_int();
  if (a.created_ms_ == 0) a.created_ms_ = now_ms();
  if (doc.contains("branches") && doc["branches"].is_array()) {
    const Json& arr = doc["branches"];
    a.branches_.reserve(arr.size());
    for (std::size_t i = 0; i < arr.size(); ++i) a.branches_.push_back(branch_from_json(arr[i]));
  }
  if (a.branches_.empty()) {
    BranchInfo root_branch;
    root_branch.id = Sha256::hex("/", 1).substr(0, 12);
    root_branch.path = "/";
    root_branch.name = "root";
    root_branch.created_ms = a.created_ms_;
    root_branch.is_node = true;
    root_branch.system = true;
    a.branches_.push_back(root_branch);
  }
  for (const auto& b : a.branches_) fs::mkdirs(a.branch_dir(b.id));
  fs::mkdirs(fs::join(a.root_, kObjectsDir));
  return a;
}

std::string Archive::meta_path() const { return fs::join(root_, kMetaFile); }

std::string Archive::branch_dir(const std::string& id) const {
  return fs::join(fs::join(root_, kBranchesDir), id);
}

std::vector<BranchInfo> Archive::branches() const { return branches_; }

std::optional<BranchInfo> Archive::find(const std::string& path) const {
  const std::string want = canonical_branch(path);
  for (const auto& b : branches_) {
    if (b.path == want) return b;
  }
  return std::nullopt;
}

bool Archive::branch_exists(const std::string& path) const { return find(path).has_value(); }

BranchInfo Archive::create_branch(const std::string& path, const std::string& name) {
  const std::string want = canonical_branch(path);
  if (want == "/") fail(3010, msg("store.branch_exists"), want);
  if (branch_exists(want)) fail(3010, msg("store.branch_exists"), want);

  /* Auto-create missing ancestors so nested paths like /a/b/c work in one call. */
  std::vector<std::string> chain;
  {
    std::string cur = want;
    while (cur != "/" && !cur.empty()) {
      chain.push_back(cur);
      const std::size_t pos = cur.find_last_of('/');
      cur = pos == 0 ? "/" : cur.substr(0, pos);
    }
  }
  std::reverse(chain.begin(), chain.end());
  for (const auto& step : chain) {
    if (branch_exists(step)) continue;
    const std::string parent_path = step.find_last_of('/') == 0 ? "/" : step.substr(0, step.find_last_of('/'));
    auto parent = find(parent_path);
    if (!parent) fail(3011, msg("store.no_parent"), parent_path);
    BranchInfo b;
    b.path = step;
    b.name = (step == want && !name.empty()) ? name : branch_name_from_path(step);
    b.parent = parent->path;
    b.created_ms = now_ms();
    b.id = Sha256::hex(step.data(), step.size()).substr(0, 12);
    for (std::size_t n = 1; n < 64; ++n) {
      bool clash = false;
      for (const auto& other : branches_) {
        if (other.id == b.id) {
          clash = true;
          break;
        }
      }
      if (!clash) break;
      const std::string salt = step + "#" + std::to_string(n);
      b.id = Sha256::hex(salt.data(), salt.size()).substr(0, 12);
    }
    b.is_node = step != want;
    fs::mkdirs(branch_dir(b.id));
    for (auto& other : branches_) {
      if (other.path == parent->path) {
        other.children.push_back(step);
        std::sort(other.children.begin(), other.children.end());
      }
    }
    branches_.push_back(b);
  }
  save_meta();
  for (const auto& b : branches_) {
    if (b.path == want) return b;
  }
  fail(3017, "internal: branch not created", want);
}

void Archive::remove_branch(const std::string& path, bool recursive) {
  const std::string want = canonical_branch(path);
  if (want == "/") fail(3012, msg("store.root_branch"), want);
  auto it = std::find_if(branches_.begin(), branches_.end(),
                         [&](const BranchInfo& b) { return b.path == want; });
  if (it == branches_.end()) fail(3013, msg("store.no_branch"), want);
  if (!it->children.empty() && !recursive) fail(3014, msg("store.has_children"), want);

  std::vector<std::string> doomed;
  std::vector<std::string> stack{it->id};
  while (!stack.empty()) {
    const std::string id = stack.back();
    stack.pop_back();
    doomed.push_back(id);
    for (const auto& b : branches_) {
      if (b.parent == want) stack.push_back(b.id);
    }
    for (auto& b : branches_) {
      if (b.id == id) {
        for (const auto& c : b.children) {
          auto child = find(c);
          if (child) stack.push_back(child->id);
        }
      }
    }
  }
  for (const auto& id : doomed) {
    fs::remove_all(branch_dir(id));
    branches_.erase(std::remove_if(branches_.begin(), branches_.end(),
                                   [&](const BranchInfo& b) { return b.id == id; }),
                    branches_.end());
  }
  for (auto& b : branches_) {
    b.children.erase(std::remove(b.children.begin(), b.children.end(), want), b.children.end());
  }
  save_meta();
}

std::optional<Snapshot> Archive::head(const std::string& path) const {
  auto b = find(path);
  if (!b || b->head_seq <= 0) return std::nullopt;
  return snapshot_at(path, b->head_seq);
}

std::optional<Snapshot> Archive::snapshot_at(const std::string& path, i64 seq) const {
  auto b = find(path);
  if (!b) return std::nullopt;
  std::string text;
  if (!fs::read_file_opt(fs::join(branch_dir(b->id), kHistoryFile), text)) return std::nullopt;
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    const std::string line = trim(text.substr(pos, nl - pos));
    pos = nl + 1;
    if (line.empty()) continue;
    auto j = Json::try_parse(line);
    if (!j) continue;
    Snapshot s = snapshot_from_json(*j);
    if (s.seq == seq) return s;
  }
  return std::nullopt;
}

std::vector<Snapshot> Archive::history(const std::string& path, std::size_t limit) const {
  std::vector<Snapshot> out;
  auto b = find(path);
  if (!b) return out;
  std::string text;
  if (!fs::read_file_opt(fs::join(branch_dir(b->id), kHistoryFile), text)) return out;
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    const std::string line = trim(text.substr(pos, nl - pos));
    pos = nl + 1;
    if (line.empty()) continue;
    auto j = Json::try_parse(line);
    if (!j) continue;
    out.push_back(snapshot_from_json(*j));
  }
  std::sort(out.begin(), out.end(), [](const Snapshot& a, const Snapshot& b2) { return a.seq < b2.seq; });
  if (limit > 0 && out.size() > limit) out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(limit));
  return out;
}

std::string Archive::commit(const std::string& path, const std::vector<Entry>& entries,
                            const std::string& message) {
  auto b = find(path);
  if (!b) fail(3013, msg("store.no_branch"), canonical_branch(path));
  Snapshot s;
  s.seq = b->head_seq + 1;
  s.branch = b->path;
  s.time_ms = now_ms();
  s.message = message;
  if (b->head_seq > 0) {
    auto prev = snapshot_at(path, b->head_seq);
    if (prev) s.parent_sha = prev->id;
  }
  s.entries = entries;
  std::sort(s.entries.begin(), s.entries.end(),
            [](const Entry& x, const Entry& y) { return x.path < y.path; });
  s.id = snapshot_id(s);

  Json line = snapshot_body(s);
  line.set("id", Json(s.id));
  const std::string text = line.dump(-1) + "\n";
  const std::string history_path = fs::join(branch_dir(b->id), kHistoryFile);
  std::string existing;
  fs::read_file_opt(history_path, existing);
  fs::write_file_atomic(history_path, existing + text);

  Json head_doc = Json::object();
  head_doc.set("head_seq", Json(s.seq));
  head_doc.set("id", Json(s.id));
  head_doc.set("time", Json(s.time_ms));
  fs::write_file_atomic(fs::join(branch_dir(b->id), kHeadFile), head_doc.dump(2));

  for (auto& other : branches_) {
    if (other.id == b->id) other.head_seq = s.seq;
  }
  save_meta();
  return s.id;
}

void Archive::materialize(const std::string& path, i64 seq, const std::string& target, Emitter* ui) {
  auto snap = snapshot_at(path, seq);
  if (!snap) fail(3015, msg("store.no_snapshot"), canonical_branch(path) + "@" + std::to_string(seq));
  fs::mkdirs(target);
  const std::size_t total = snap->entries.size();
  std::size_t done = 0;
  for (const auto& e : snap->entries) {
    const std::string dest = fs::join(target, e.path);
    if (e.dir) {
      fs::mkdirs(dest);
    } else {
      std::string data;
      if (!objects_.get(e.sha, data)) fail(3016, msg("store.missing_object"), e.sha);
      fs::mkdirs(fs::parent(dest));
      fs::write_file_atomic(dest, data);
    }
    ++done;
    if (ui && total > 0 && (done % 64 == 0 || done == total)) {
      ui->progress("materialize", static_cast<long long>(done), static_cast<long long>(total), 0, 0);
    }
  }
  if (ui) ui->clear_status();
}

std::vector<Entry> Archive::scan_working_tree(const std::string& root,
                                              std::vector<std::string>* deleted,
                                              std::set<std::string>* added) const {
  std::vector<Entry> out;
  std::vector<std::string> stack;
  const std::string base = fs::absolute(root);
  stack.push_back(base);
  while (!stack.empty()) {
    const std::string dir = stack.back();
    stack.pop_back();
    const bool at_root = (dir == base);
    for (const auto& name : fs::list_dir(dir)) {
      if (name == ".git" || name == ".inv" || name == kMetaFile || name == kLegacyMetaFile ||
          name == kObjectsDir)
        continue;
      // Archive metadata lives in <root>/branches; it is not user content, so a
      // commit must not capture it and diff/status must not report it as changed.
      if (at_root && name == kBranchesDir) continue;
      const std::string full = fs::join(dir, name);
      std::string rel = full.substr(base.size());
      while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) rel.erase(rel.begin());
      std::replace(rel.begin(), rel.end(), '\\', '/');
      if (rel.empty()) continue;
      if (fs::is_dir(full)) {
        Entry e;
        e.path = rel;
        e.dir = true;
        e.mtime = fs::mtime_ms(full);
        out.push_back(e);
        stack.push_back(full);
      } else if (fs::is_file(full)) {
        Entry e;
        e.path = rel;
        e.size = static_cast<u64>(fs::size(full));
        e.mtime = fs::mtime_ms(full);
        std::string data = fs::read_file(full);
        e.sha = Sha256::hex(data.data(), data.size());
        e.crc = crc64(data.data(), data.size());
        out.push_back(e);
      }
    }
  }
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b2) { return a.path < b2.path; });

  if (deleted) deleted->clear();
  if (added) added->clear();
  auto b = find("/");
  if (b) {
    auto snap = head("/");
    if (snap) {
      std::map<std::string, const Entry*> prev;
      for (const auto& e : snap->entries) prev[e.path] = &e;
      std::vector<Entry> changed;
      changed.reserve(out.size());
      for (const auto& e : out) {
        auto it = prev.find(e.path);
        if (it == prev.end()) {
          changed.push_back(e);
          if (added) added->insert(e.path);
          continue;
        }
        const Entry& old = *it->second;
        if (e.dir != old.dir || e.size != old.size || e.sha != old.sha) changed.push_back(e);
      }
      if (deleted) {
        std::set<std::string> now;
        for (const auto& e : out) now.insert(e.path);
        for (const auto& kv : prev) {
          if (now.find(kv.first) == now.end()) deleted->push_back(kv.first);
        }
      }
      out.swap(changed);
    } else if (added) {
      /* No snapshot on the root branch yet: every scanned path is new. */
      for (const auto& e : out) added->insert(e.path);
    }
  }
  return out;
}

void Archive::save_meta() {
  Json doc = Json::object();
  doc.set("version", Json(1));
  doc.set("name", Json(name_));
  doc.set("type", Json(std::string(archive_type_name(type_))));
  doc.set("id", Json(archive_id_));
  doc.set("created", Json(created_ms_));
  Json arr = Json::array();
  for (const auto& b : branches_) arr.push(branch_to_json(b));
  doc.set("branches", arr);
  if (extra_.is_object() && extra_.size() > 0) {
    for (const auto& key : extra_.keys()) doc.set(key, extra_[key]);
  }
  fs::mkdirs(root_);
  fs::write_file_atomic(meta_path(), doc.dump(2) + "\n");
}

void Archive::reload() {
  Archive fresh = Archive::open(root_);
  name_ = fresh.name_;
  type_ = fresh.type_;
  archive_id_ = fresh.archive_id_;
  created_ms_ = fresh.created_ms_;
  branches_ = fresh.branches_;
  objects_ = ObjectStore(root_);
}

}  // namespace vd
