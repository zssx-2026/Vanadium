#include <cstdlib>
#include <string>

#include "vanadium/core.h"
#include "vanadium/remote.h"

namespace vd::remote {
namespace {

/* %APPDATA%/inv/credentials.json — resolved from the environment so the path is
   stable no matter what the shell passes as its working directory. */
std::string credentials_path() {
  const char* appdata = std::getenv("APPDATA");
  if (!appdata || !*appdata) {
    fail(1, "APPDATA is not set", "cannot locate credentials.json");
  }
  return fs::join(fs::join(appdata, "inv"), "credentials.json");
}

/* A missing or corrupt store is not an error: the user simply has no token yet. */
Json load_store() {
  std::string text;
  if (!fs::read_file_opt(credentials_path(), text) || trim(text).empty()) return Json::object();
  auto parsed = Json::try_parse(text);
  if (!parsed || !parsed->is_object()) return Json::object();
  return *parsed;
}

}  // namespace

std::string load_token(const std::string& host) {
  Json store = load_store();
  if (!store.contains("hosts")) return {};
  const Json& hosts = store["hosts"];
  if (!hosts.is_object() || !hosts.contains(host)) return {};
  const Json& entry = hosts[host];
  if (!entry.is_object() || !entry.contains("token")) return {};
  return entry["token"].as_string_or("");
}

void save_token(const std::string& host, const std::string& user, const std::string& token) {
  Json store = load_store();
  if (!store.contains("hosts") || !store["hosts"].is_object()) {
    store.set("hosts", Json::object());
  }
  Json entry = Json::object();
  entry.set("user", Json(user));
  entry.set("token", Json(token));
  entry.set("saved_at", Json(iso_time(now_ms())));
  store["hosts"].set(host, entry);

  std::string path = credentials_path();
  fs::mkdirs(fs::parent(path));
  fs::write_file_atomic(path, store.dump(2));
}

}  // namespace vd::remote
