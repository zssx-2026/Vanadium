#include "vanadium/registry.h"

#include <algorithm>
#include <string>

#include "vanadium/core.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace vd {
namespace {

#ifdef _WIN32
std::wstring wide_path(const std::string& p) {
  if (p.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
  if (n <= 0) return std::wstring();
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, out.data(), n);
  while (!out.empty() && out.back() == L'\0') out.pop_back();
  return out;
}

void hide_path(const std::string& p) {
  const std::wstring w = wide_path(p);
  if (w.empty()) return;
  const DWORD attrs = GetFileAttributesW(w.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return;
  SetFileAttributesW(w.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
}
#else
void hide_path(const std::string&) {}
#endif

std::string entry_json(const RegistryEntry& e) {
  Json j = Json::object();
  j.set("version", Json(1));
  j.set("id", Json(e.id));
  j.set("archive", Json(e.archive));
  j.set("name", Json(e.name));
  j.set("type", Json(e.type));
  j.set("path", Json(e.path));
  j.set("dir", Json(e.dir));
  j.set("created", Json(e.created_ms));
  j.set("registered", Json(e.registered_ms));
  j.set("head", Json(e.head_seq));
  return j.dump(2) + "\n";
}

bool parse_entry(const Json& j, RegistryEntry* out) {
  if (!j.is_object()) return false;
  const Json& id = j["id"];
  if (!id.is_string() || id.as_string().empty()) return false;
  out->id = id.as_string();
  out->archive = j["archive"].as_string_or("");
  out->name = j["name"].as_string_or("");
  out->type = j["type"].as_string_or("");
  out->path = j["path"].as_string_or("/");
  out->dir = j["dir"].as_string_or("");
  out->created_ms = j["created"].as_int(0);
  out->registered_ms = j["registered"].as_int(0);
  out->head_seq = j["head"].as_int(0);
  return true;
}

}  // namespace

std::string registry_dir(const std::string& root) { return fs::join(root, kRegistryDir); }

bool workspace_initialized(const std::string& root) {
  return !root.empty() && fs::is_dir(registry_dir(root));
}

bool ensure_registry_dir(const std::string& root) {
  const std::string dir = registry_dir(root);
  if (fs::is_dir(dir)) return false;
  fs::mkdirs(dir);
  hide_path(dir);
  return true;
}

std::string registry_path(const std::string& root, const std::string& id) {
  return fs::join(registry_dir(root), id + kRegistrySuffix);
}

bool write_registry(const std::string& root, const RegistryEntry& entry, std::string* err) {
  if (entry.id.empty()) {
    if (err) *err = "registry entry has no id";
    return false;
  }
  if (!workspace_initialized(root)) {
    if (err) *err = "workspace is not initialised (missing " + std::string(kRegistryDir) + ")";
    return false;
  }
  try {
    fs::write_file_atomic(registry_path(root, entry.id), entry_json(entry));
    return true;
  } catch (const Failure& f) {
    if (err) *err = f.message + (f.detail.empty() ? "" : " | " + f.detail);
    return false;
  }
}

std::optional<RegistryEntry> read_registry(const std::string& root, const std::string& id) {
  std::string text;
  if (!fs::read_file_opt(registry_path(root, id), text)) return std::nullopt;
  const std::optional<Json> j = Json::try_parse(text);
  if (!j) return std::nullopt;
  RegistryEntry e;
  if (!parse_entry(*j, &e)) return std::nullopt;
  return e;
}

std::vector<RegistryEntry> list_registry(const std::string& root) {
  std::vector<RegistryEntry> out;
  const std::string dir = registry_dir(root);
  if (!fs::is_dir(dir)) return out;
  for (const std::string& name : fs::list_dir(dir)) {
    if (!ends_with(name, kRegistrySuffix)) continue;
    const std::string id = name.substr(0, name.size() - std::string(kRegistrySuffix).size());
    if (id.empty()) continue;
    if (auto e = read_registry(root, id)) out.push_back(*e);
  }
  std::sort(out.begin(), out.end(),
            [](const RegistryEntry& a, const RegistryEntry& b) { return a.id < b.id; });
  return out;
}

bool remove_registry(const std::string& root, const std::string& id) {
  const std::string p = registry_path(root, id);
  if (!fs::is_file(p)) return false;
  fs::remove_file(p);
  return true;
}

}  // namespace vd
