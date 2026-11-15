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

void apply_timeouts(HINTERNET req, int timeout_ms) {
  int total = timeout_ms > 0 ? timeout_ms : 60000;
  int resolve = std::min(total, 15000);
  int connect = std::min(total, 30000);
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
  apply_timeouts(req.h, timeout_ms);
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
                   const std::function<void(u64, u64)>& on_chunk) {
  UrlParts parts = parse_url(url);
  HINTERNET session = session_for(opt.user_agent);
  Handle req = open_request(session, parts, opt, opt.timeout_ms);
  send_request(req.h, opt);

  int status = query_status(req.h);
  std::string raw = query_raw_headers(req.h);
  auto headers = parse_headers(raw);
  if (!(status == 200 || status == 206)) {
    std::string detail;
    read_body(req.h, detail, 8192);
    fail(1, "HTTP request failed: status " + std::to_string(status),
         url + " | " + trim(detail).substr(0, 400));
  }

  std::wstring wpath = widen(path);
  HANDLE file = CreateFileW(wpath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    fail(1, "cannot create output file", path + " | " + winhttp_error_text(GetLastError()));
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

ProbeInfo probe_remote(const std::string& url, int timeout_ms) {
  UrlParts parts = parse_url(url);
  HttpOptions opt;
  opt.method = "GET";
  opt.headers.emplace_back("Range", "bytes=0-0");
  opt.headers.emplace_back("Accept-Encoding", "identity");
  HINTERNET session = session_for(opt.user_agent);
  Handle req = open_request(session, parts, opt, timeout_ms);
  send_request(req.h, opt);

  ProbeInfo info;
  info.status = query_status(req.h);
  auto headers = parse_headers(query_raw_headers(req.h));
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
