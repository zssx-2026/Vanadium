#pragma once
#include <functional>
#include <string>
#include <vector>
#include "core.h"
#include "ui.h"

namespace vd::remote {

struct HttpResponse {
  int status = 0;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string header(const std::string& name) const;
  bool ok() const { return status >= 200 && status < 300; }
};

struct HttpOptions {
  std::string method = "GET";
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  int timeout_ms = 60000;
  int retries = 2;
  std::string user_agent = "inv/0.1 (+vanadium)";
  bool follow_redirects = true;
};

HttpResponse request(const std::string& url, const HttpOptions& opt);
HttpResponse get(const std::string& url, const std::string& token = {},
                 int timeout_ms = 60000);
HttpResponse post_json(const std::string& url, const std::string& token,
                       const Json& body, int timeout_ms = 60000);
HttpResponse put_json(const std::string& url, const std::string& token,
                      const Json& body, int timeout_ms = 60000);
HttpResponse upload_file(const std::string& url, const std::string& token,
                         const std::string& path, const std::string& content_type,
                         int timeout_ms = 600000);
HttpResponse del(const std::string& url, const std::string& token,
                 int timeout_ms = 60000);

/* Range request: [from, to] inclusive. Returns body bytes. */
bool fetch_range(const std::string& url, u64 from, u64 to, std::string& out,
                 int timeout_ms = 120000);

/* IDM-style segmented download: split the object into N ranges, fetch concurrently,
   merge in order, report per-thread progress. Returns false on failure. */
struct DownloadReport {
  u64 bytes = 0;
  i64 elapsed_ms = 0;
  int threads = 1;
  int retries = 0;
};
bool download_segmented(const std::string& url, const std::string& out_path, int threads,
                        Emitter* ui, DownloadReport* report);

class Github {
 public:
  explicit Github(std::string token, std::string api_base = "https://api.github.com");
  Json user();
  Json repo(const std::string& owner, const std::string& name);
  Json list_releases(const std::string& owner, const std::string& name, int per_page = 30);
  Json get_release(const std::string& owner, const std::string& name, const std::string& tag);
  Json create_release(const std::string& owner, const std::string& name,
                      const std::string& tag, const std::string& title,
                      const std::string& body, bool prerelease = false);
  Json update_release(const std::string& owner, const std::string& name,
                      long long id, const Json& patch);
  bool delete_release(const std::string& owner, const std::string& name, long long id);
  Json upload_asset(const std::string& upload_url_template, const std::string& path,
                    const std::string& asset_name);
  bool delete_asset(const std::string& owner, const std::string& name, long long asset_id);
  std::string token() const { return token_; }

 private:
  std::string token_;
  std::string api_;
};

/* Parse https://github.com/<owner>/<repo> or git@github.com:owner/repo.git */
bool parse_repo_url(const std::string& url, std::string& owner, std::string& repo);

/* Credentials store: %APPDATA%/inv/credentials.json */
std::string load_token(const std::string& host);
void save_token(const std::string& host, const std::string& user, const std::string& token);

}  // namespace vd::remote
