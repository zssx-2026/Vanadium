#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/remote.h"

namespace vd::remote {
namespace {

std::string url_encode(const std::string& text) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size() * 3);
  for (unsigned char c : text) {
    bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                 c == '-' || c == '_' || c == '.' || c == '~';
    if (plain) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0f];
    }
  }
  return out;
}

std::string content_type_for(const std::string& path) {
  std::string ext = to_lower(fs::ext(path));
  if (ext == ".json") return "application/json";
  if (ext == ".zip") return "application/zip";
  if (ext == ".gz" || ext == ".tgz") return "application/gzip";
  if (ext == ".txt" || ext == ".md") return "text/plain; charset=utf-8";
  if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
  if (ext == ".sha256" || ext == ".sig") return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

/* PATCH is only used for release edits, so it lives here instead of in the
   public helpers. */
HttpResponse patch_json(const std::string& url, const std::string& token, const Json& body) {
  HttpOptions opt;
  opt.method = "PATCH";
  opt.timeout_ms = 60000;
  opt.headers.emplace_back("Content-Type", "application/json");
  opt.headers.emplace_back("Accept", "application/vnd.github+json");
  opt.headers.emplace_back("X-GitHub-Api-Version", "2022-11-28");
  if (!token.empty()) opt.headers.emplace_back("Authorization", "Bearer " + token);
  opt.body = body.dump();
  HttpResponse resp = request(url, opt);
  if (!resp.ok()) {
    fail(4, "HTTP " + std::to_string(resp.status) + " for " + url,
         trim(resp.body).substr(0, 300));
  }
  return resp;
}

Json parse_or_fail(const HttpResponse& resp, const std::string& what) {
  auto parsed = Json::try_parse(resp.body);
  if (!parsed) {
    fail(6, "invalid JSON in response for " + what,
         trim(resp.body).substr(0, 300));
  }
  return *parsed;
}

}  // namespace

Github::Github(std::string token, std::string api_base)
    : token_(std::move(token)), api_(std::move(api_base)) {
  while (!api_.empty() && api_.back() == '/') api_.pop_back();
}

Json Github::user() {
  return parse_or_fail(get(api_ + "/user", token_), "GET /user");
}

Json Github::repo(const std::string& owner, const std::string& name) {
  std::string url = api_ + "/repos/" + url_encode(owner) + "/" + url_encode(name);
  return parse_or_fail(get(url, token_), "GET repo");
}

Json Github::list_releases(const std::string& owner, const std::string& name, int per_page) {
  int page_size = per_page <= 0 ? 30 : per_page;
  if (page_size > 100) page_size = 100;
  std::string url = api_ + "/repos/" + url_encode(owner) + "/" + url_encode(name) +
                    "/releases?per_page=" + std::to_string(page_size);
  return parse_or_fail(get(url, token_), "GET releases");
}

Json Github::get_release(const std::string& owner, const std::string& name,
                         const std::string& tag) {
  std::string url = api_ + "/repos/" + url_encode(owner) + "/" + url_encode(name) +
                    "/releases/tags/" + url_encode(tag);
  return parse_or_fail(get(url, token_), "GET release by tag");
}

Json Github::create_release(const std::string& owner, const std::string& name,
                            const std::string& tag, const std::string& title,
                            const std::string& body, bool prerelease) {
  Json payload = Json::object();
  payload.set("tag_name", Json(tag));
  payload.set("name", Json(title.empty() ? tag : title));
  payload.set("body", Json(body));
  payload.set("prerelease", Json(prerelease));
  payload.set("draft", Json(false));
  std::string url = api_ + "/repos/" + url_encode(owner) + "/" + url_encode(name) + "/releases";
  return parse_or_fail(post_json(url, token_, payload), "POST release");
}

Json Github::update_release(const std::string& owner, const std::string& name, long long id,
                            const Json& patch) {
  std::string url = api_ + "/repos/" + url_encode(owner) + "/" + url_encode(name) +
                    "/releases/" + std::to_string(id);
  return parse_or_fail(patch_json(url, token_, patch), "PATCH release");
}

bool Github::delete_release(const std::string& owner, const std::string& name, long long id) {
  std::string url = api_ + "/repos/" + url_encode(owner) + "/" + url_encode(name) +
                    "/releases/" + std::to_string(id);
  HttpResponse resp = del(url, token_);
  return resp.ok();
}

Json Github::upload_asset(const std::string& upload_url_template, const std::string& path,
                          const std::string& asset_name) {
  std::string url = upload_url_template;
  size_t brace = url.find('{');
  if (brace != std::string::npos) url.resize(brace);
  if (url.empty()) fail(7, "empty upload_url template", upload_url_template);
  std::string name = asset_name.empty() ? fs::filename(path) : asset_name;
  url += (url.find('?') == std::string::npos ? "?" : "&");
  url += "name=" + url_encode(name);

  HttpResponse resp = upload_file(url, token_, path, content_type_for(path), 600000);
  /* GitHub answers 201 Created with the asset object. */
  if (resp.status != 201) {
    fail(8, "asset upload returned HTTP " + std::to_string(resp.status),
         trim(resp.body).substr(0, 300));
  }
  return parse_or_fail(resp, "POST asset");
}

bool Github::delete_asset(const std::string& owner, const std::string& name, long long asset_id) {
  std::string url = api_ + "/repos/" + url_encode(owner) + "/" + url_encode(name) +
                    "/releases/assets/" + std::to_string(asset_id);
  HttpResponse resp = del(url, token_);
  return resp.ok();
}

bool parse_repo_url(const std::string& url, std::string& owner, std::string& repo) {
  std::string text = trim(url);
  std::string path;

  if (starts_with(text, "git@")) {
    size_t colon = text.find(':');
    if (colon == std::string::npos) return false;
    path = text.substr(colon + 1);
  } else if (starts_with(text, "ssh://")) {
    size_t at = text.find('@');
    size_t colon = text.find(':', at == std::string::npos ? 0 : at);
    if (colon == std::string::npos) return false;
    path = text.substr(colon + 1);
  } else if (starts_with(text, "https://") || starts_with(text, "http://")) {
    size_t scheme = text.find("://");
    size_t slash = text.find('/', scheme + 3);
    if (slash == std::string::npos) return false;
    path = text.substr(slash + 1);
  } else {
    /* Bare owner/repo shorthand. */
    path = text;
  }

  size_t query = path.find_first_of("?#");
  if (query != std::string::npos) path.resize(query);

  std::vector<std::string> parts = split(path, '/');
  parts.erase(std::remove_if(parts.begin(), parts.end(),
                             [](const std::string& s) { return s.empty(); }),
              parts.end());
  if (parts.size() < 2) return false;
  owner = parts[0];
  repo = parts[1];
  if (ends_with(to_lower(repo), ".git")) repo.resize(repo.size() - 4);
  if (repo.empty()) return false;
  return true;
}

}  // namespace vd::remote
