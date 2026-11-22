#include <chrono>
#include <random>
#include <thread>

#include "vanadium/core.h"
#include "vanadium/remote.h"
#include "transport.h"

namespace vd::remote {
namespace {

using detail::RawResponse;

bool iequals(const std::string& a, const std::string& b) { return to_lower(a) == to_lower(b); }

bool retryable_status(int status) {
  return status == 408 || status == 429 || status == 500 || status == 502 || status == 503 ||
         status == 504;
}

std::string body_summary(const std::string& body) {
  std::string text = trim(body);
  if (text.size() > 500) text = text.substr(0, 500) + "...";
  return text;
}

void sleep_backoff(int attempt) {
  int base = 250 << (attempt < 4 ? attempt : 4); /* 250, 500, 1000, 2000, 4000 ms */
  if (base > 4000) base = 4000;
  std::random_device rd;
  std::uniform_int_distribution<int> jitter(0, base / 4 + 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(base + jitter(rd)));
}

/* Executes one logical request: follows redirects but never retries. */
RawResponse perform(const std::string& url, const HttpOptions& opt) {
  std::string current = url;
  std::string method = opt.method.empty() ? "GET" : opt.method;
  std::string body = opt.body;

  for (int hop = 0; hop <= detail::kMaxRedirects; ++hop) {
    HttpOptions hop_opt = opt;
    hop_opt.method = method;
    hop_opt.body = body;
    RawResponse resp = detail::send_once(current, hop_opt, opt.timeout_ms);

    bool redirect = resp.status == 301 || resp.status == 302 || resp.status == 303 ||
                    resp.status == 307 || resp.status == 308;
    if (!redirect || !opt.follow_redirects || resp.location.empty()) return resp;
    if (resp.status == 303 || ((resp.status == 301 || resp.status == 302) && method == "POST")) {
      method = "GET";
      body.clear();
    }
    current = detail::resolve_location(current, resp.location);
  }
  fail(3, "too many HTTP redirects", url);
}

HttpResponse to_response(RawResponse&& raw) {
  HttpResponse out;
  out.status = raw.status;
  out.body = std::move(raw.body);
  out.headers = std::move(raw.headers);
  return out;
}

void add_api_headers(HttpOptions& opt, const std::string& token) {
  opt.headers.emplace_back("Accept", "application/vnd.github+json");
  opt.headers.emplace_back("X-GitHub-Api-Version", "2022-11-28");
  if (!token.empty()) opt.headers.emplace_back("Authorization", "Bearer " + token);
}

void expect_ok(const HttpResponse& resp, const std::string& url) {
  if (resp.ok()) return;
  fail(4, "HTTP " + std::to_string(resp.status) + " for " + url, body_summary(resp.body));
}

}  // namespace

std::string HttpResponse::header(const std::string& name) const {
  for (const auto& kv : headers) {
    if (iequals(kv.first, name)) return kv.second;
  }
  return {};
}

HttpResponse request(const std::string& url, const HttpOptions& opt) {
  int attempts = opt.retries < 0 ? 1 : opt.retries + 1;
  std::string last_error;
  for (int attempt = 0; attempt < attempts; ++attempt) {
    try {
      RawResponse raw = perform(url, opt);
      if (retryable_status(raw.status) && attempt + 1 < attempts) {
        last_error = "HTTP " + std::to_string(raw.status);
        sleep_backoff(attempt);
        continue;
      }
      return to_response(std::move(raw));
    } catch (const Failure& f) {
      last_error = f.message;
      if (!f.detail.empty()) last_error += " | " + f.detail;
      if (attempt + 1 >= attempts) throw;
      sleep_backoff(attempt);
    }
  }
  fail(5, "request failed after retries", url + " | " + last_error);
}

HttpResponse get(const std::string& url, const std::string& token, int timeout_ms) {
  HttpOptions opt;
  opt.method = "GET";
  opt.timeout_ms = timeout_ms;
  add_api_headers(opt, token);
  HttpResponse resp = request(url, opt);
  expect_ok(resp, url);
  return resp;
}

HttpResponse post_json(const std::string& url, const std::string& token, const Json& body,
                       int timeout_ms) {
  HttpOptions opt;
  opt.method = "POST";
  opt.timeout_ms = timeout_ms;
  opt.headers.emplace_back("Content-Type", "application/json");
  add_api_headers(opt, token);
  opt.body = body.dump();
  HttpResponse resp = request(url, opt);
  expect_ok(resp, url);
  return resp;
}

HttpResponse put_json(const std::string& url, const std::string& token, const Json& body,
                      int timeout_ms) {
  HttpOptions opt;
  opt.method = "PUT";
  opt.timeout_ms = timeout_ms;
  opt.headers.emplace_back("Content-Type", "application/json");
  add_api_headers(opt, token);
  opt.body = body.dump();
  HttpResponse resp = request(url, opt);
  expect_ok(resp, url);
  return resp;
}

HttpResponse upload_file(const std::string& url, const std::string& token, const std::string& path,
                         const std::string& content_type, int timeout_ms) {
  std::string payload = fs::read_file(path);
  HttpOptions opt;
  opt.method = "POST";
  opt.timeout_ms = timeout_ms;
  opt.retries = 1;
  opt.headers.emplace_back("Content-Type",
                           content_type.empty() ? "application/octet-stream" : content_type);
  add_api_headers(opt, token);
  opt.body = std::move(payload);
  HttpResponse resp = request(url, opt);
  expect_ok(resp, url);
  return resp;
}

HttpResponse del(const std::string& url, const std::string& token, int timeout_ms) {
  HttpOptions opt;
  opt.method = "DELETE";
  opt.timeout_ms = timeout_ms;
  add_api_headers(opt, token);
  HttpResponse resp = request(url, opt);
  expect_ok(resp, url);
  return resp;
}

bool fetch_range(const std::string& url, u64 from, u64 to, std::string& out, int timeout_ms) {
  if (to < from) return false;
  HttpOptions opt;
  opt.method = "GET";
  opt.timeout_ms = timeout_ms;
  opt.retries = 1;
  opt.headers.emplace_back("Range",
                           "bytes=" + std::to_string(from) + "-" + std::to_string(to));
  /* Byte offsets are only meaningful on an unencoded body. */
  opt.headers.emplace_back("Accept-Encoding", "identity");

  HttpResponse resp = request(url, opt);
  u64 want = to - from + 1;
  if (resp.status == 206) {
    out = std::move(resp.body);
    return out.size() == want;
  }
  if (resp.status == 200) {
    /* Server ignored the Range header: crop the full body locally. */
    if (resp.body.size() < static_cast<size_t>(to + 1)) return false;
    out.assign(resp.body, static_cast<size_t>(from), static_cast<size_t>(want));
    return true;
  }
  return false;
}

}  // namespace vd::remote
