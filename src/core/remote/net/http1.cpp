#include "http1.h"

#include <algorithm>
#include <cstring>

#include "socket.h"

namespace vd::remote::net {
namespace {

bool iequals(const std::string& a, const std::string& b) { return to_lower(a) == to_lower(b); }

std::string header_or_empty(const std::vector<std::pair<std::string, std::string>>& headers,
                            const std::string& name) {
  for (const auto& kv : headers) {
    if (iequals(kv.first, name)) return kv.second;
  }
  return {};
}

std::vector<std::string> split_crlf(const std::string& block) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (;;) {
    std::size_t pos = block.find("\r\n", start);
    if (pos == std::string::npos) {
      out.push_back(block.substr(start));
      break;
    }
    out.push_back(block.substr(start, pos - start));
    start = pos + 2;
  }
  return out;
}

u64 parse_u64(const std::string& text, const std::string& what) {
  std::string value = trim(text);
  if (value.empty()) throw Failure(1, "malformed http response", what + " is empty");
  u64 out = 0;
  for (char c : value) {
    if (c < '0' || c > '9') {
      throw Failure(1, "malformed http response", what + " is not a number: " + value);
    }
    out = out * 10 + static_cast<u64>(c - '0');
    if (out > (1ull << 60)) throw Failure(1, "malformed http response", what + " is too large");
  }
  return out;
}

}  // namespace

std::string ResponseHead::header(const std::string& name) const {
  return header_or_empty(headers, name);
}

bool ResponseHead::has_header(const std::string& name) const {
  for (const auto& kv : headers) {
    if (iequals(kv.first, name)) return true;
  }
  return false;
}

Connection::Connection(Uri uri, bool insecure_skip_verify, int connect_ms, int io_timeout_ms)
    : uri_(std::move(uri)) {
  key_ = uri_.scheme + "://" + uri_.authority();
  if (connect_ms <= 0) connect_ms = 30000;
  if (io_timeout_ms <= 0) io_timeout_ms = 60000;
  sock_ = tcp_connect(uri_.host, uri_.port, connect_ms);
  if (uri_.secure()) {
    TlsOptions options;
    options.insecure_skip_verify = insecure_skip_verify;
    options.handshake_timeout_ms = std::max(connect_ms, io_timeout_ms);
    tls_ = std::make_unique<TlsStream>(sock_, uri_.host, options);
    tls_->handshake();
    /* TlsStream owns the socket from here on. */
    sock_ = INVALID_SOCKET;
  }
  last_used_ms_ = now_ms();
}

Connection::~Connection() {
  tls_.reset();
  if (sock_ != INVALID_SOCKET) {
    close_socket(sock_);
    sock_ = INVALID_SOCKET;
  }
}

bool Connection::fill(int timeout_ms) {
  if (eof_ || closed_) return false;
  char tmp[16384];
  std::size_t n = 0;
  if (tls_) {
    n = tls_->read(tmp, sizeof(tmp), timeout_ms);
  } else {
    n = recv_some(sock_, tmp, sizeof(tmp), timeout_ms);
  }
  if (n == 0) {
    eof_ = true;
    closed_ = true;
    return false;
  }
  buffer_.append(tmp, n);
  return true;
}

bool Connection::ensure(std::size_t n, int timeout_ms) {
  while (available() < n) {
    if (!fill(timeout_ms)) return false;
  }
  return true;
}

void Connection::consume(std::size_t n) {
  pos_ += n;
  if (pos_ > 65536 && pos_ * 2 >= buffer_.size()) {
    buffer_.erase(0, pos_);
    pos_ = 0;
  }
}

bool Connection::read_line(std::string& line, int timeout_ms) {
  for (;;) {
    std::size_t end = buffer_.find("\r\n", pos_);
    if (end != std::string::npos) {
      line = buffer_.substr(pos_, end - pos_);
      consume(end - pos_ + 2);
      return true;
    }
    if (available() > 256 * 1024) {
      throw Failure(1, "malformed http response", "line too long (12030)");
    }
    if (!fill(timeout_ms)) return false;
  }
}

bool Connection::read_head(ResponseHead& head, int timeout_ms) {
  for (;;) {
    std::size_t end = buffer_.find("\r\n\r\n", pos_);
    while (end == std::string::npos) {
      if (available() > 256 * 1024) {
        throw Failure(1, "malformed http response", "header block too large (12030)");
      }
      if (!fill(timeout_ms)) {
        if (available() == 0) return false;
        throw Failure(1, "malformed http response", "connection closed inside headers (12030)");
      }
      end = buffer_.find("\r\n\r\n", pos_);
    }
    std::string block = buffer_.substr(pos_, end - pos_);
    consume(end - pos_ + 4);

    std::vector<std::string> lines = split_crlf(block);
    if (lines.empty() || lines[0].empty()) {
      throw Failure(1, "malformed http response", "empty status line (12030)");
    }
    ResponseHead parsed;
    const std::string& status_line = lines[0];
    std::size_t sp1 = status_line.find(' ');
    if (sp1 == std::string::npos) {
      throw Failure(1, "malformed http response", "bad status line: " + status_line);
    }
    parsed.version = status_line.substr(0, sp1);
    std::size_t sp2 = status_line.find(' ', sp1 + 1);
    std::string code = status_line.substr(
        sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1);
    parsed.status = static_cast<int>(parse_u64(code, "status code"));
    if (sp2 != std::string::npos) parsed.reason = status_line.substr(sp2 + 1);

    for (std::size_t i = 1; i < lines.size(); ++i) {
      const std::string& line = lines[i];
      if (line.empty()) continue;
      if (line[0] == ' ' || line[0] == '\t') {
        if (!parsed.headers.empty()) parsed.headers.back().second += " " + trim(line);
        continue;
      }
      std::size_t colon = line.find(':');
      if (colon == std::string::npos) continue;
      parsed.headers.emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
    }

    /* 100-continue and other interim responses carry no body: read on. */
    if (parsed.status >= 100 && parsed.status < 200) continue;
    head = std::move(parsed);
    return true;
  }
}

void Connection::read_exact(std::size_t n,
                            const std::function<void(const char*, std::size_t)>& sink,
                            int timeout_ms) {
  while (n > 0) {
    if (!ensure(1, timeout_ms)) {
      throw Failure(1, "truncated response body",
                    "connection closed with " + std::to_string(n) + " bytes missing (12030)");
    }
    std::size_t take = std::min(n, available());
    sink(buffer_.data() + pos_, take);
    consume(take);
    n -= take;
  }
}

void Connection::send(const HttpRequest& request) {
  std::string method = request.method.empty() ? "GET" : request.method;
  head_request_ = method == "HEAD";

  auto overridden = [&](const char* name) {
    for (const auto& kv : request.headers) {
      if (iequals(kv.first, name)) return true;
    }
    return false;
  };

  std::string head;
  if (request.target.empty()) {
    throw Failure(1, "malformed http request", "request target is empty");
  }
  head += method;
  head += ' ';
  head += request.target;
  head += " HTTP/1.1\r\n";
  if (!overridden("Host")) head += "Host: " + uri_.authority() + "\r\n";
  if (!overridden("User-Agent") && !request.user_agent.empty()) {
    head += "User-Agent: " + request.user_agent + "\r\n";
  }
  if (!overridden("Accept")) head += "Accept: */*\r\n";
  /* Byte offsets must map onto the wire, so never negotiate compression. */
  if (!overridden("Accept-Encoding")) head += "Accept-Encoding: identity\r\n";
  if (!overridden("Connection")) head += "Connection: keep-alive\r\n";
  bool body_method = method == "POST" || method == "PUT" || method == "PATCH";
  if ((!request.body.empty() || body_method) && !overridden("Content-Length")) {
    head += "Content-Length: " + std::to_string(request.body.size()) + "\r\n";
  }
  for (const auto& kv : request.headers) {
    head += kv.first + ": " + kv.second + "\r\n";
  }
  head += "\r\n";

  if (tls_) {
    tls_->write(head.data(), head.size(), request.timeout_ms);
    if (!request.body.empty()) tls_->write(request.body.data(), request.body.size(), request.timeout_ms);
  } else {
    const std::string where = "http write " + uri_.host + ":" + std::to_string(uri_.port);
    send_all(sock_, head.data(), head.size(), request.timeout_ms, where);
    if (!request.body.empty()) {
      send_all(sock_, request.body.data(), request.body.size(), request.timeout_ms, where);
    }
  }
  touch();
}

void Connection::read_body(const ResponseHead& head,
                           const std::function<void(const char*, std::size_t)>& sink,
                           int timeout_ms) {
  bool keep_alive = head.version != "HTTP/1.0";
  std::string connection = to_lower(head.header("Connection"));
  if (connection.find("close") != std::string::npos) keep_alive = false;
  if (head.version == "HTTP/1.0" && connection.find("keep-alive") != std::string::npos) {
    keep_alive = true;
  }

  bool no_body = head_request_ || head.status == 204 || head.status == 304 || head.status == 205;
  if (no_body) {
    reusable_ = reusable_ && keep_alive;
    return;
  }

  std::string transfer = to_lower(head.header("Transfer-Encoding"));
  if (transfer.find("chunked") != std::string::npos) {
    for (;;) {
      std::string line;
      if (!read_line(line, timeout_ms)) {
        throw Failure(1, "truncated response body", "chunk header missing (12030)");
      }
      std::size_t semi = line.find(';');
      if (semi != std::string::npos) line = line.substr(0, semi);
      line = trim(line);
      if (line.empty()) continue;
      u64 size = 0;
      for (char c : line) {
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else throw Failure(1, "malformed http response", "bad chunk size: " + line);
        size = size * 16 + static_cast<u64>(digit);
      }
      if (size == 0) {
        /* Trailer section ends with an empty line. */
        for (;;) {
          std::string trailer;
          if (!read_line(trailer, timeout_ms) || trailer.empty()) break;
        }
        break;
      }
      read_exact(static_cast<std::size_t>(size), sink, timeout_ms);
      std::string crlf;
      if (!read_line(crlf, timeout_ms)) {
        throw Failure(1, "truncated response body", "chunk terminator missing (12030)");
      }
    }
    reusable_ = reusable_ && keep_alive;
    return;
  }

  std::string length = head.header("Content-Length");
  if (!length.empty()) {
    u64 n = parse_u64(length, "Content-Length");
    read_exact(static_cast<std::size_t>(n), sink, timeout_ms);
    reusable_ = reusable_ && keep_alive;
    return;
  }

  /* No length: the body runs until the peer closes, so the connection dies. */
  reusable_ = false;
  while (fill(timeout_ms)) {
    if (available() > 0) {
      sink(buffer_.data() + pos_, available());
      consume(available());
    }
  }
}

bool Connection::peer_closed() {
  if (closed_ || eof_) return true;
  if (tls_) return false; /* encrypted records make a peek meaningless */
  fd_set set;
  FD_ZERO(&set);
  FD_SET(sock_, &set);
  timeval tv{0, 0};
  int rc = ::select(0, &set, nullptr, nullptr, &tv);
  if (rc <= 0) return false;
  char byte = 0;
  int n = ::recv(sock_, &byte, 1, MSG_PEEK);
  return n == 0;
}

Pool& Pool::get() {
  static Pool pool;
  return pool;
}

std::shared_ptr<Connection> Pool::acquire(const Uri& uri, bool insecure_skip_verify, int connect_ms,
                                          int io_timeout_ms) {
  const std::string key = uri.scheme + "://" + uri.authority();
  i64 now = now_ms();
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = idle_.find(key);
  if (it != idle_.end()) {
    auto& entries = it->second;
    while (!entries.empty()) {
      Entry entry = entries.back();
      entries.pop_back();
      if (now - entry.since > 60000) continue;
      if (!entry.conn || entry.conn->closed() || !entry.conn->reusable()) continue;
      if (entry.conn->peer_closed()) continue;
      entry.conn->touch();
      return entry.conn;
    }
    idle_.erase(it);
  }
  return std::make_shared<Connection>(uri, insecure_skip_verify, connect_ms, io_timeout_ms);
}

void Pool::release(std::shared_ptr<Connection> conn) {
  if (!conn || conn->closed() || !conn->reusable()) return;
  i64 now = now_ms();
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t total = 0;
  for (const auto& kv : idle_) total += kv.second.size();
  if (total >= 16) {
    /* Drop the oldest idle connection anywhere in the pool. */
    std::string victim;
    i64 oldest = now;
    std::size_t index = 0;
    for (auto& kv : idle_) {
      for (std::size_t i = 0; i < kv.second.size(); ++i) {
        if (kv.second[i].since <= oldest) {
          oldest = kv.second[i].since;
          victim = kv.first;
          index = i;
        }
      }
    }
    if (!victim.empty()) {
      auto& entries = idle_[victim];
      entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
      if (entries.empty()) idle_.erase(victim);
    }
  }
  conn->touch();
  idle_[conn->key()].push_back(Entry{std::move(conn), now});
}

std::size_t Pool::idle_count() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t total = 0;
  for (const auto& kv : idle_) total += kv.second.size();
  return total;
}

void Pool::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  idle_.clear();
}

Exchange exchange_once(const std::string& url, const HttpRequest& request) {
  Uri uri = parse_uri(url);
  Exchange result;
  for (int attempt = 0; attempt < 2; ++attempt) {
    std::shared_ptr<Connection> conn = Pool::get().acquire(
        uri, request.insecure_skip_verify, request.connect_ms, request.timeout_ms);
    try {
      conn->send(request);
      if (!conn->read_head(result.head, request.timeout_ms)) {
        if (attempt == 0) continue; /* stale pooled connection: retry fresh */
        throw Failure(1, "empty response", "peer closed without a response (12030)");
      }
      result.body.clear();
      conn->read_body(
          result.head, [&](const char* data, std::size_t n) { result.body.append(data, n); },
          request.timeout_ms);
      Pool::get().release(std::move(conn));
      return result;
    } catch (const Failure&) {
      if (attempt == 0 && conn->closed()) continue;
      throw;
    }
  }
  throw Failure(1, "empty response", "peer closed without a response (12030)");
}

}  // namespace vd::remote::net
