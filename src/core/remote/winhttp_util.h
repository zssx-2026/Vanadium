#pragma once
/* WinHTTP plumbing shared by the remote layer.
   URL parsing, session cache, single HTTP exchange, streaming body to file,
   and a one-byte range probe used by the segmented downloader. */
#include <windows.h>
#include <winhttp.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/remote.h"

namespace vd::remote::detail {

struct UrlParts {
  bool https = true;
  std::wstring host;   /* host only, never contains the port */
  INTERNET_PORT port = 0;
  std::wstring target; /* path plus optional query, always starts with '/' */
  std::string to_string() const;
};

/* Throws vd::Failure when the URL is not absolute http(s). */
UrlParts parse_url(const std::string& url);

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
std::string winhttp_error_text(unsigned long code);

struct RawResponse {
  int status = 0;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  std::string location;
};

/* Executes exactly one exchange: no retries, no redirect following. */
RawResponse send_once(const std::string& url, const HttpOptions& opt, int timeout_ms);

/* Streams the response body into path. Returns bytes written.
   Throws vd::Failure on transport error or a non-2xx status. */
u64 stream_to_file(const std::string& url, const HttpOptions& opt, const std::string& path,
                   const std::function<void(u64, u64)>& on_chunk);

struct ProbeInfo {
  u64 total = 0; /* 0 when the server did not report a length */
  bool ranges = false;
  int status = 0;
};

/* One-byte range probe: detects object size and range support without
   downloading the payload. */
ProbeInfo probe_remote(const std::string& url, int timeout_ms);

}  // namespace vd::remote::detail
