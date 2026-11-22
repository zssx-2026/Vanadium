#pragma once
/* Native transport contract for the remote layer. This replaces the former
   system HTTP plumbing: URL resolution, a single HTTP exchange, streaming a body to
   a file with redirect following, and a one-byte range probe. Everything below
   is implemented on Winsock plus Schannel (see net/). */
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/remote.h"

namespace vd::remote::detail {

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);

struct RawResponse {
  int status = 0;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  std::string location;
};

/* Executes exactly one exchange: no retries, no redirect following. */
RawResponse send_once(const std::string& url, const HttpOptions& opt, int timeout_ms);

/* Redirects allowed for a single logical request. */
constexpr int kMaxRedirects = 5;

/* Resolves a Location header against the URL that produced it. */
std::string resolve_location(const std::string& base_url, const std::string& location);

/* Response metadata for a streamed transfer, filled before any byte is written
   so callers can react to a server that ignored Range (status 200). */
struct StreamMeta {
  int status = 0;
  u64 content_length = 0;
  std::string etag;          /* verbatim, quotes included; empty when absent */
  std::string last_modified; /* verbatim; empty when absent */
};

/* Streams the response body into path, following up to kMaxRedirects hops so a
   CDN hand-off (GitHub release assets answer 302 first) still lands on bytes.
   Returns bytes written. Throws vd::Failure on transport error or non-2xx.
   append=true keeps existing bytes and writes after them, so a retried range can
   resume instead of starting over. When append=true and the server answers 200
   (Range ignored: the object changed or ranges are unsupported) nothing is
   written, path is left untouched, meta->status is 200 and 0 is returned so the
   caller can restart the transfer from zero instead of splicing two objects. */
u64 stream_to_file(const std::string& url, const HttpOptions& opt, const std::string& path,
                   const std::function<void(u64, u64)>& on_chunk, bool append = false,
                   StreamMeta* meta = nullptr);

struct ProbeInfo {
  u64 total = 0; /* 0 when the server did not report a length */
  bool ranges = false;
  int status = 0;
  std::string etag;          /* verbatim, quotes included; empty when absent */
  std::string last_modified; /* verbatim; empty when absent */
};

/* One-byte range probe: detects object size and range support without
   downloading the payload. resolve_ms/connect_ms override HttpOptions defaults
   (0 = derive from timeout_ms). */
ProbeInfo probe_remote(const std::string& url, int timeout_ms, int resolve_ms = 0,
                       int connect_ms = 0);

/* Transport status code carried in a Failure detail (e.g. "... (12002)"),
   or 0 when the failure did not come from the transport. */
unsigned long transport_error_code(const Failure& f);

/* True for failures worth another attempt: timeouts (12002), connect failures
   (12029) and aborted connections (12030). */
bool transport_retryable(const Failure& f);

}  // namespace vd::remote::detail
