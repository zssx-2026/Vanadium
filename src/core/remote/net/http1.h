#pragma once
/* HTTP/1.1 client: request serialisation, response parsing (Content-Length,
   chunked, connection close), keep-alive connection pool and HEAD support.
   Redirect policy and retries live in transport.cpp so the same primitives can
   be reused by the segmented downloader. */
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "tls.h"
#include "uri.h"
#include "vanadium/core.h"

namespace vd::remote::net {

/* Product token sent when the caller does not override the user agent. */
inline constexpr const char* kDefaultUserAgent = "inv/0.4.0";

struct HttpRequest {
  std::string method = "GET";
  std::string target = "/";    /* request-target sent on the wire (path?query) */
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  std::string user_agent = kDefaultUserAgent;
  int timeout_ms = 60000;
  int resolve_ms = 0;
  int connect_ms = 0;
  bool insecure_skip_verify = false; /* test only */
};

struct ResponseHead {
  int status = 0;
  std::string version = "HTTP/1.1";
  std::string reason;
  std::vector<std::pair<std::string, std::string>> headers;

  std::string header(const std::string& name) const;
  bool has_header(const std::string& name) const;
};

/* One pooled connection. A connection is only returned to the pool when the
   response body was fully consumed and the peer did not ask for a close. */
class Connection {
 public:
  Connection(Uri uri, bool insecure_skip_verify, int connect_ms, int io_timeout_ms);
  ~Connection();
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  void send(const HttpRequest& request);
  /* Reads the status line and headers, skipping 1xx interim responses.
     Returns false when the peer closed before sending anything. */
  bool read_head(ResponseHead& head, int timeout_ms);
  /* Streams the decoded body into sink. Throws on truncation. */
  void read_body(const ResponseHead& head,
                 const std::function<void(const char*, std::size_t)>& sink, int timeout_ms);

  bool reusable() const { return reusable_; }
  bool closed() const { return closed_; }
  const std::string& key() const { return key_; }
  i64 last_used_ms() const { return last_used_ms_; }
  void touch() { last_used_ms_ = now_ms(); }
  /* True when a pooled idle socket was closed by the peer in the meantime. */
  bool peer_closed();

 private:
  bool fill(int timeout_ms);
  bool ensure(std::size_t n, int timeout_ms);
  bool read_line(std::string& line, int timeout_ms);
  void consume(std::size_t n);
  std::size_t available() const { return buffer_.size() - pos_; }
  void read_exact(std::size_t n, const std::function<void(const char*, std::size_t)>& sink,
                  int timeout_ms);

  Uri uri_;
  std::string key_;
  SOCKET sock_ = INVALID_SOCKET;
  std::unique_ptr<TlsStream> tls_;
  std::string buffer_;
  std::size_t pos_ = 0;
  bool eof_ = false;
  bool closed_ = false;
  bool head_request_ = false;
  bool reusable_ = true;
  i64 last_used_ms_ = 0;
};

/* Process wide idle connection pool: key scheme://host:port, 60 s idle expiry,
   at most 16 idle connections. */
class Pool {
 public:
  static Pool& get();
  std::shared_ptr<Connection> acquire(const Uri& uri, bool insecure_skip_verify, int connect_ms,
                                      int io_timeout_ms);
  void release(std::shared_ptr<Connection> conn);
  std::size_t idle_count();
  void clear();

 private:
  Pool() = default;
  struct Entry {
    std::shared_ptr<Connection> conn;
    i64 since = 0;
  };
  std::mutex mutex_;
  std::map<std::string, std::vector<Entry>> idle_;
};

/* Convenience: perform one exchange (no redirects) and collect the body. */
struct Exchange {
  ResponseHead head;
  std::string body;
};
Exchange exchange_once(const std::string& url, const HttpRequest& request);

}  // namespace vd::remote::net
