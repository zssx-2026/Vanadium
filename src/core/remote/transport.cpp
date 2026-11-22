#include "transport.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "net/http1.h"
#include "net/uri.h"
#include "vanadium/core.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace vd::remote::detail {
namespace {

using net::Connection;
using net::HttpRequest;
using net::Pool;
using net::ResponseHead;

int pick(int value, int fallback) { return value > 0 ? value : fallback; }

HttpRequest make_request(const HttpOptions& opt, int timeout_ms) {
  HttpRequest req;
  req.method = opt.method.empty() ? "GET" : opt.method;
  req.headers = opt.headers;
  req.body = opt.body;
  if (!opt.user_agent.empty()) req.user_agent = opt.user_agent;
  req.timeout_ms = pick(timeout_ms, opt.timeout_ms);
  req.resolve_ms = opt.resolve_ms;
  req.connect_ms = opt.connect_ms;
  return req;
}

bool is_redirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

u64 parse_u64_len(const std::string& text) {
  u64 value = 0;
  bool any = false;
  for (char c : text) {
    if (c == ' ' || c == '\t') continue;
    if (c < '0' || c > '9') break;
    value = value * 10 + static_cast<u64>(c - '0');
    any = true;
  }
  return any ? value : 0;
}

std::string raw_header(const RawResponse& resp, const char* name) {
  const std::string want = to_lower(name);
  for (const auto& kv : resp.headers) {
    if (to_lower(kv.first) == want) return kv.second;
  }
  return {};
}

std::string body_summary(const std::string& body) {
  std::string text = trim(body);
  if (text.size() > 300) text = text.substr(0, 300) + "...";
  return text;
}

/* Reads a body into a string, stopping after limit bytes so an error page or a
   redirect stub can be described without buffering a whole payload. */
std::string drain_body(const std::shared_ptr<Connection>& conn, const ResponseHead& head,
                       std::size_t limit, int timeout_ms) {
  std::string out;
  try {
    conn->read_body(
        head,
        [&](const char* data, std::size_t n) {
          if (out.size() >= limit) return;
          std::size_t take = std::min(n, limit - out.size());
          out.append(data, take);
        },
        timeout_ms);
  } catch (const Failure&) {
    /* A truncated error body must not mask the status code. */
  }
  return out;
}

/* Sends one request, retrying once on a stale pooled connection. */
std::shared_ptr<Connection> open_exchange(const net::Uri& uri, const HttpRequest& req,
                                          ResponseHead& head) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    std::shared_ptr<Connection> conn =
        Pool::get().acquire(uri, false, req.connect_ms, req.timeout_ms);
    try {
      conn->send(req);
      if (conn->read_head(head, req.timeout_ms)) return conn;
      if (attempt == 0) continue;
      throw Failure(1, "empty response", "peer closed without a response (12030)");
    } catch (const Failure&) {
      if (attempt == 0 && conn->closed()) continue;
      throw;
    }
  }
  throw Failure(1, "empty response", "peer closed without a response (12030)");
}

}  // namespace

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int need = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (need <= 0) return {};
  std::wstring out(static_cast<std::size_t>(need), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), need);
  return out;
}

std::string narrow(const std::wstring& s) {
  if (s.empty()) return {};
  int need = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                 nullptr, nullptr);
  if (need <= 0) return {};
  std::string out(static_cast<std::size_t>(need), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), need, nullptr,
                      nullptr);
  return out;
}

RawResponse send_once(const std::string& url, const HttpOptions& opt, int timeout_ms) {
  HttpRequest req = make_request(opt, timeout_ms);
  net::Exchange ex = net::exchange_once(url, req);
  RawResponse out;
  out.status = ex.head.status;
  out.headers = std::move(ex.head.headers);
  out.body = std::move(ex.body);
  out.location = ex.head.header("Location");
  return out;
}

std::string resolve_location(const std::string& base_url, const std::string& location) {
  return net::resolve_reference(base_url, location);
}

u64 stream_to_file(const std::string& url, const HttpOptions& opt, const std::string& path,
                   const std::function<void(u64, u64)>& on_chunk, bool append, StreamMeta* meta) {
  HttpRequest base = make_request(opt, opt.timeout_ms);
  std::string current = url;

  for (int hop = 0; hop <= kMaxRedirects; ++hop) {
    net::Uri uri = net::parse_uri(current);
    ResponseHead head;
    std::shared_ptr<Connection> conn = open_exchange(uri, base, head);

    const std::string location = head.header("Location");
    if (is_redirect(head.status) && !location.empty()) {
      if (hop == kMaxRedirects) throw Failure(3, "too many HTTP redirects", url);
      /* The hop body is a stub; dropping the connection is cheaper than
         draining it and keeps the pool free of one-shot sockets. */
      current = net::resolve_reference(current, location);
      continue;
    }

    const bool success = head.status >= 200 && head.status < 300;
    if (!success) {
      std::string body = drain_body(conn, head, 4096, base.timeout_ms);
      Failure f(2, "HTTP " + std::to_string(head.status) + " for " + current,
                body_summary(body));
      throw f;
    }

    StreamMeta local;
    local.status = head.status;
    local.content_length = parse_u64_len(head.header("Content-Length"));
    local.etag = head.header("ETag");
    local.last_modified = head.header("Last-Modified");
    if (meta) *meta = local;

    if (append && head.status == 200) {
      /* Range ignored on a resume: the object changed or ranges are gone. Do
         not splice a full body behind existing bytes; the caller restarts. */
      return 0;
    }

    FILE* out = _wfopen(widen(path).c_str(), append ? L"ab" : L"wb");
    if (!out) throw Failure(1, "cannot open " + path + " for writing", "errno=" + std::to_string(errno));

    u64 done = 0;
    const u64 total_hint = local.content_length;
    try {
      conn->read_body(
          head,
          [&](const char* data, std::size_t n) {
            if (std::fwrite(data, 1, n, out) != n) {
              throw Failure(1, "write failed for " + path, "errno=" + std::to_string(errno));
            }
            done += n;
            if (on_chunk) on_chunk(done, total_hint);
          },
          base.timeout_ms);
    } catch (...) {
      std::fclose(out);
      throw;
    }
    if (std::fclose(out) != 0) {
      throw Failure(1, "close failed for " + path, "errno=" + std::to_string(errno));
    }
    Pool::get().release(std::move(conn));
    return done;
  }
  throw Failure(3, "too many HTTP redirects", url);
}

ProbeInfo probe_remote(const std::string& url, int timeout_ms, int resolve_ms, int connect_ms) {
  HttpOptions opt;
  opt.method = "GET";
  opt.timeout_ms = timeout_ms;
  opt.resolve_ms = resolve_ms;
  opt.connect_ms = connect_ms;
  opt.headers.emplace_back("Range", "bytes=0-0");
  /* Byte offsets are only meaningful on an unencoded body. */
  opt.headers.emplace_back("Accept-Encoding", "identity");

  ProbeInfo info;
  std::string current = url;
  RawResponse resp;
  for (int hop = 0; hop <= kMaxRedirects; ++hop) {
    resp = send_once(current, opt, timeout_ms);
    if (is_redirect(resp.status) && !resp.location.empty() && hop < kMaxRedirects) {
      current = net::resolve_reference(current, resp.location);
      continue;
    }
    break;
  }
  info.status = resp.status;
  info.etag = raw_header(resp, "ETag");
  info.last_modified = raw_header(resp, "Last-Modified");

  if (resp.status == 206) {
    info.ranges = true;
    /* Content-Range: bytes 0-0/12345 */
    std::string range = raw_header(resp, "Content-Range");
    std::size_t slash = range.find('/');
    if (slash != std::string::npos) {
      std::string total = range.substr(slash + 1);
      if (total != "*") info.total = parse_u64_len(total);
    }
  } else if (resp.status == 200) {
    info.total = parse_u64_len(raw_header(resp, "Content-Length"));
    info.ranges = to_lower(raw_header(resp, "Accept-Ranges")).find("bytes") != std::string::npos;
  } else if (resp.status == 416) {
    info.ranges = true;
    /* Content-Range: bytes star-slash total (unsatisfiable range). */
    std::string range = raw_header(resp, "Content-Range");
    std::size_t slash = range.find('/');
    if (slash != std::string::npos) {
      std::string total = range.substr(slash + 1);
      if (total != "*") info.total = parse_u64_len(total);
    }
  }
  return info;
}

unsigned long transport_error_code(const Failure& f) {
  auto scan = [](const std::string& text) -> unsigned long {
    /* "(12002)" as produced by the socket layer. */
    std::size_t open = text.rfind('(');
    if (open != std::string::npos) {
      std::size_t close = text.find(')', open);
      if (close != std::string::npos) {
        unsigned long value = 0;
        bool any = false;
        for (std::size_t i = open + 1; i < close; ++i) {
          char c = text[i];
          if (c < '0' || c > '9') {
            any = false;
            break;
          }
          value = value * 10 + static_cast<unsigned long>(c - '0');
          any = true;
        }
        if (any) return value;
      }
    }
    /* "error 12002" as produced by the former transport plumbing. */
    std::size_t at = text.rfind("error ");
    if (at != std::string::npos) {
      unsigned long value = 0;
      bool any = false;
      for (std::size_t i = at + 6; i < text.size(); ++i) {
        char c = text[i];
        if (c < '0' || c > '9') break;
        value = value * 10 + static_cast<unsigned long>(c - '0');
        any = true;
      }
      if (any) return value;
    }
    return 0;
  };
  unsigned long code = scan(f.detail);
  if (code == 0) code = scan(f.message);
  return code;
}

bool transport_retryable(const Failure& f) {
  const unsigned long code = transport_error_code(f);
  return code == 12002 || code == 12029 || code == 12030;
}

}  // namespace vd::remote::detail

namespace vd {

/* Backend identifier for the doctor output. Declared in remote.h by the kernel
   owner; kept here so the remote layer owns the fact. */
const char* transport_backend_name() { return "native (winsock2 + schannel)"; }

}  // namespace vd
