#include "winhttp_util.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace vd::remote::detail {
namespace {

constexpr size_t kReadChunk = 64 * 1024;
constexpr size_t kMaxBody = 256ull * 1024 * 1024; /* guard for buffered responses */

/* Owns a WinHTTP handle. */
struct Handle {
  HINTERNET h = nullptr;
  explicit Handle(HINTERNET v = nullptr) : h(v) {}
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept : h(other.h) { other.h = nullptr; }
  Handle& operator=(Handle&& other) noexcept {
    if (this != &other) {
      if (h) WinHttpCloseHandle(h);
      h = other.h;
      other.h = nullptr;
    }
    return *this;
  }
  explicit operator bool() const { return h != nullptr; }
};

/* Sessions are cached per user agent for the process lifetime; WinHTTP handles
   are thread safe and reusing them avoids a TLS handshake per call. */
std::mutex g_session_mu;
std::map<std::string, HINTERNET> g_sessions;

HINTERNET session_for(const std::string& user_agent) {
  std::lock_guard<std::mutex> lock(g_session_mu);
  auto it = g_sessions.find(user_agent);
  if (it != g_sessions.end()) return it->second;
  std::wstring ua = widen(user_agent);
  HINTERNET h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!h) {
    h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (!h) fail(1, "WinHttpOpen failed", winhttp_error_text(GetLastError()));
  g_sessions[user_agent] = h;
  return h;
}

void apply_timeouts(HINTERNET req, int timeout_ms, int resolve_ms, int connect_ms) {
  int total = timeout_ms > 0 ? timeout_ms : 60000;
  /* Weak networks need a patient connect phase; callers raise connect_ms
     instead of the whole request budget. Clamp to the total so a stray value
     cannot make a phase outlive the request. */
  int resolve = resolve_ms > 0 ? resolve_ms : std::min(total, 15000);
  int connect = connect_ms > 0 ? connect_ms : std::min(total, 30000);
  if (resolve > total) resolve = total;
  if (connect > total) connect = total;
  WinHttpSetTimeouts(req, resolve, connect, total, total);
}

int query_status(HINTERNET req) {
  DWORD code = 0;
  DWORD len = sizeof(code);
  if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX)) {
    fail(1, "WinHttpQueryHeaders(status) failed", winhttp_error_text(GetLastError()));
  }
  return static_cast<int>(code);
}

std::string query_raw_headers(HINTERNET req) {
  DWORD len = 0;
  WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                      &len, WINHTTP_NO_HEADER_INDEX);
  if (len == 0) return {};
  std::wstring buf(len / sizeof(wchar_t) + 2, L'\0');
  if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                           buf.data(), &len, WINHTTP_NO_HEADER_INDEX)) {
    return {};
  }
  return narrow(buf.c_str());
}

std::vector<std::pair<std::string, std::string>> parse_headers(const std::string& raw) {
  std::vector<std::pair<std::string, std::string>> out;
  size_t pos = 0;
  bool first = true;
  while (pos < raw.size()) {
    size_t eol = raw.find("\r\n", pos);
    std::string line =
        raw.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
    pos = (eol == std::string::npos) ? raw.size() : eol + 2;
    if (first) {
      first = false; /* status line */
      continue;
    }
    if (line.empty()) continue;
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    out.emplace_back(line.substr(0, colon), trim(line.substr(colon + 1)));
  }
  return out;
}

std::wstring build_header_block(const std::vector<std::pair<std::string, std::string>>& headers) {
  std::wstring block;
  for (const auto& kv : headers) {
    if (kv.first.empty()) continue;
    block += widen(kv.first);
    block += L": ";
    block += widen(kv.second);
    block += L"\r\n";
  }
  return block;
}

/* Opens a request handle for url. Returns false when the URL cannot be reached;
   transport failures throw. */
Handle open_request(HINTERNET session, const UrlParts& parts, const HttpOptions& opt,
                    int timeout_ms) {
  Handle conn(WinHttpConnect(session, parts.host.c_str(), parts.port, 0));
  if (!conn) fail(1, "WinHttpConnect failed", winhttp_error_text(GetLastError()) + " host=" +
                                                   narrow(parts.host));
  std::wstring method = widen(opt.method.empty() ? "GET" : opt.method);
  Handle req(WinHttpOpenRequest(conn.h, method.c_str(), parts.target.c_str(), nullptr,
                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                parts.https ? WINHTTP_FLAG_SECURE : 0));
  if (!req) fail(1, "WinHttpOpenRequest failed", winhttp_error_text(GetLastError()));
  apply_timeouts(req.h, timeout_ms, opt.resolve_ms, opt.connect_ms);
  return req;
}

void send_request(HINTERNET req, const HttpOptions& opt) {
  std::wstring block = build_header_block(opt.headers);
  LPCWSTR headers = block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : block.c_str();
  DWORD headers_len = block.empty() ? 0 : static_cast<DWORD>(-1);
  void* body = opt.body.empty() ? WINHTTP_NO_REQUEST_DATA
                                : const_cast<char*>(opt.body.data());
  DWORD body_len = static_cast<DWORD>(opt.body.size());
  if (!WinHttpSendRequest(req, headers, headers_len, body, body_len, body_len, 0)) {
    fail(1, "WinHttpSendRequest failed", winhttp_error_text(GetLastError()));
  }
  if (!WinHttpReceiveResponse(req, nullptr)) {
    fail(1, "WinHttpReceiveResponse failed", winhttp_error_text(GetLastError()));
  }
}

void read_body(HINTERNET req, std::string& out, size_t max_bytes) {
  char buf[kReadChunk];
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req, &avail)) {
      fail(1, "WinHttpQueryDataAvailable failed", winhttp_error_text(GetLastError()));
    }
    if (avail == 0) break;
    DWORD want = static_cast<DWORD>(std::min<size_t>(avail, kReadChunk));
    DWORD got = 0;
    if (!WinHttpReadData(req, buf, want, &got)) {
      fail(1, "WinHttpReadData failed", winhttp_error_text(GetLastError()));
    }
    if (got == 0) break;
    out.append(buf, got);
    if (max_bytes && out.size() > max_bytes) {
      fail(2, "response body too large for a buffered request",
           "limit=" + std::to_string(max_bytes));
    }
  }
}

std::string header_value(const std::vector<std::pair<std::string, std::string>>& headers,
                         const std::string& name) {
  std::string want = to_lower(name);
  for (const auto& kv : headers) {
    if (to_lower(kv.first) == want) return kv.second;
  }
  return {};
}

}  // namespace

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                              nullptr, nullptr);
  if (n <= 0) return {};
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                      nullptr);
  return s;
}

unsigned long transport_error_code(const Failure& f) {
  /* winhttp_error_text renders "text (12002)" when FormatMessage has a string
     and "error 12002" when it does not, so accept both shapes. */
  const std::string& text = f.detail;
  for (size_t i = 0; i < text.size(); ++i) {
    size_t j = std::string::npos;
    if (text[i] == '(') {
      j = i + 1;
    } else if (text.compare(i, 6, "error ") == 0) {
      j = i + 6;
    }
    if (j == std::string::npos || j >= text.size()) continue;
    unsigned long value = 0;
    bool any = false;
    while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
      value = value * 10 + static_cast<unsigned long>(text[j] - '0');
      ++j;
      any = true;
    }
    if (any) return value;
  }
  return 0;
}

bool transport_retryable(const Failure& f) {
  const unsigned long code = transport_error_code(f);
  switch (code) {
    case 12002: /* ERROR_WINHTTP_TIMEOUT */
    case 12007: /* ERROR_WINHTTP_NAME_NOT_RESOLVED - the resolver itself times out
                   on a weak link, so one more attempt is worth it */
    case 12029: /* ERROR_WINHTTP_CANNOT_CONNECT */
    case 12030: /* ERROR_WINHTTP_CONNECTION_ERROR */
    case 12031: /* ERROR_WINHTTP_RESEND_REQUEST - the stack itself asks for a retry */
    case 12152: /* ERROR_WINHTTP_INVALID_SERVER_RESPONSE - an edge closing a
                   connection mid-headers looks exactly like this (observed on
                   release-assets.githubusercontent.com) */
      return true;
    default:
      /* Certificate failures (12157/12175), malformed URLs and auth errors are
         permanent: retrying them only hides the real cause. */
      return false;
  }
}

std::string winhttp_error_text(unsigned long code) {
  wchar_t* buf = nullptr;
  DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, static_cast<DWORD>(code),
                           MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                           reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
  std::string text;
  if (n > 0 && buf) text = narrow(std::wstring(buf, n));
  if (buf) LocalFree(buf);
  text = trim(text);
  if (text.empty()) text = "error " + std::to_string(code);
  else text += " (" + std::to_string(code) + ")";
  return text;
}

std::string UrlParts::to_string() const {
  std::string out = https ? "https://" : "http://";
  out += narrow(host);
  bool default_port = (https && port == 443) || (!https && port == 80);
  if (!default_port) out += ":" + std::to_string(port);
  out += narrow(target);
  return out;
}

UrlParts parse_url(const std::string& url) {
  UrlParts p;
  std::string rest;
  if (starts_with(url, "https://")) {
    p.https = true;
    rest = url.substr(8);
  } else if (starts_with(url, "http://")) {
    p.https = false;
    rest = url.substr(7);
  } else {
    fail(1, "unsupported URL scheme, expected http:// or https://", url);
  }
  size_t hash = rest.find('#');
  if (hash != std::string::npos) rest.resize(hash);

  size_t slash = rest.find('/');
  std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string target = slash == std::string::npos ? "/" : rest.substr(slash);
  if (authority.empty()) fail(1, "URL has no host", url);

  size_t at = authority.rfind('@');
  if (at != std::string::npos) authority = authority.substr(at + 1);

  std::string host = authority;
  std::string port_text;
  if (!authority.empty() && authority[0] == '[') {
    size_t close = authority.find(']');
    if (close == std::string::npos) fail(1, "malformed IPv6 host", url);
    host = authority.substr(0, close + 1);
    if (close + 1 < authority.size() && authority[close + 1] == ':') {
      port_text = authority.substr(close + 2);
    }
  } else {
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
      host = authority.substr(0, colon);
      port_text = authority.substr(colon + 1);
    }
  }
  if (host.empty()) fail(1, "URL has no host", url);

  int port = p.https ? 443 : 80;
  if (!port_text.empty()) {
    int parsed = 0;
    for (char c : port_text) {
      if (c < '0' || c > '9') fail(1, "invalid port in URL", url);
      parsed = parsed * 10 + (c - '0');
    }
    if (parsed <= 0 || parsed > 65535) fail(1, "port out of range in URL", url);
    port = parsed;
  }
  p.host = widen(host);
  p.port = static_cast<INTERNET_PORT>(port);
  if (target.empty() || target[0] != '/') target = "/" + target;
  p.target = widen(target);
  return p;
}

std::string resolve_location(const std::string& base_url, const std::string& location) {
  if (starts_with(location, "http://") || starts_with(location, "https://")) return location;
  UrlParts parts = parse_url(base_url);
  std::string origin = parts.to_string();
  size_t scheme_end = origin.find("://");
  size_t path_start = origin.find('/', scheme_end == std::string::npos ? 0 : scheme_end + 3);
  std::string root = path_start == std::string::npos ? origin : origin.substr(0, path_start);
  if (starts_with(location, "//")) {
    return (parts.https ? "https:" : "http:") + location;
  }
  if (!location.empty() && location[0] == '/') return root + location;
  /* Relative to the directory of the current target. */
  std::string target = path_start == std::string::npos ? std::string("/") : origin.substr(path_start);
  size_t cut = target.find('?');
  if (cut != std::string::npos) target.resize(cut);
  size_t slash = target.rfind('/');
  if (slash != std::string::npos) target.resize(slash + 1);
  return root + target + location;
}

RawResponse send_once(const std::string& url, const HttpOptions& opt, int timeout_ms) {
  UrlParts parts = parse_url(url);
  HINTERNET session = session_for(opt.user_agent);
  Handle req = open_request(session, parts, opt, timeout_ms);
  send_request(req.h, opt);

  RawResponse out;
  out.status = query_status(req.h);
  out.headers = parse_headers(query_raw_headers(req.h));
  out.location = header_value(out.headers, "location");
  read_body(req.h, out.body, kMaxBody);
  return out;
}

u64 stream_to_file(const std::string& url, const HttpOptions& opt, const std::string& path,
                   const std::function<void(u64, u64)>& on_chunk, bool append,
                   StreamMeta* meta) {
  /* Follow redirects here as well: object stores (GitHub release assets, S3)
     answer 302 to a signed CDN URL, and a Range header must survive the hop. */
  std::string current = url;
  Handle req;
  int status = 0;
  std::vector<std::pair<std::string, std::string>> headers;
  int hop = 0;
  for (;;) {
    UrlParts parts = parse_url(current);
    HINTERNET session = session_for(opt.user_agent);
    req = open_request(session, parts, opt, opt.timeout_ms);
    send_request(req.h, opt);

    status = query_status(req.h);
    headers = parse_headers(query_raw_headers(req.h));
    const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 ||
                          status == 308;
    const std::string location = header_value(headers, "location");
    if (!redirect || !opt.follow_redirects || location.empty()) break;
    if (hop == kMaxRedirects) {
      fail(3, "too many HTTP redirects", current);
    }
    current = resolve_location(current, location);
    ++hop;
    req = Handle(); /* close before the next hop */
  }
  if (!(status == 200 || status == 206)) {
    std::string detail;
    read_body(req.h, detail, 8192);
    fail(1, "HTTP request failed: status " + std::to_string(status),
         current + " | " + trim(detail).substr(0, 400));
  }

  u64 total = 0;
  std::string cl = header_value(headers, "content-length");
  if (!cl.empty()) {
    try {
      total = std::stoull(cl);
    } catch (...) {
      total = 0;
    }
  }
  if (meta) {
    meta->status = status;
    meta->content_length = total;
    meta->etag = header_value(headers, "etag");
    meta->last_modified = header_value(headers, "last-modified");
  }

  /* A 200 answer to a resumed range means the server ignored Range: either the
     object was replaced (If-Range validator no longer matches) or ranges are
     unsupported. Write nothing and let the caller restart this part from zero;
     appending the full object behind the old bytes would corrupt it silently. */
  if (append && status == 200) return 0;

  const bool resume = append && status == 206;
  std::wstring wpath = widen(path);
  HANDLE file = CreateFileW(wpath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            resume ? OPEN_ALWAYS : CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    fail(1, "cannot create output file", path + " | " + winhttp_error_text(GetLastError()));
  }
  if (resume) {
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(file, zero, nullptr, FILE_END)) {
      CloseHandle(file);
      fail(1, "cannot seek to end of partial file", path);
    }
  }

  char buf[kReadChunk];
  u64 written = 0;
  bool ok = true;
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req.h, &avail)) {
      ok = false;
      break;
    }
    if (avail == 0) break;
    DWORD want = static_cast<DWORD>(std::min<size_t>(avail, kReadChunk));
    DWORD got = 0;
    if (!WinHttpReadData(req.h, buf, want, &got) || got == 0) {
      ok = false;
      break;
    }
    DWORD written_now = 0;
    if (!WriteFile(file, buf, got, &written_now, nullptr) || written_now != got) {
      CloseHandle(file);
      fail(1, "write failed", path + " | " + winhttp_error_text(GetLastError()));
    }
    written += written_now;
    if (on_chunk) on_chunk(written, total);
  }
  CloseHandle(file);
  if (!ok) {
    fail(1, "transfer interrupted", url + " after " + std::to_string(written) + " bytes");
  }
  return written;
}

ProbeInfo probe_remote(const std::string& url, int timeout_ms, int resolve_ms, int connect_ms) {
  UrlParts parts = parse_url(url);
  HttpOptions opt;
  opt.method = "GET";
  opt.resolve_ms = resolve_ms;
  opt.connect_ms = connect_ms;
  opt.headers.emplace_back("Range", "bytes=0-0");
  opt.headers.emplace_back("Accept-Encoding", "identity");
  HINTERNET session = session_for(opt.user_agent);
  Handle req = open_request(session, parts, opt, timeout_ms);
  send_request(req.h, opt);

  ProbeInfo info;
  info.status = query_status(req.h);
  auto headers = parse_headers(query_raw_headers(req.h));
  /* Validators for If-Range resume. header_value() matches names case
     insensitively and returns the value verbatim, so a quoted ETag keeps its
     quotes (If-Range requires the exact entity tag). */
  info.etag = header_value(headers, "etag");
  info.last_modified = header_value(headers, "last-modified");
  auto num = [](const std::string& text) -> u64 {
    try {
      return std::stoull(trim(text));
    } catch (...) {
      return 0;
    }
  };
  std::string content_range = header_value(headers, "content-range");
  if (info.status == 206 && !content_range.empty()) {
    size_t slash = content_range.rfind('/');
    if (slash != std::string::npos && slash + 1 < content_range.size()) {
      std::string total_text = content_range.substr(slash + 1);
      if (total_text != "*") info.total = num(total_text);
    }
    info.ranges = true;
  } else if (info.status == 200) {
    info.total = num(header_value(headers, "content-length"));
    std::string accept = to_lower(header_value(headers, "accept-ranges"));
    info.ranges = accept.find("bytes") != std::string::npos;
  } else if (info.status == 416) {
    size_t slash = content_range.rfind('/');
    if (slash != std::string::npos && slash + 1 < content_range.size()) {
      info.total = num(content_range.substr(slash + 1));
    }
  }
  return info;
}

}  // namespace vd::remote::detail
