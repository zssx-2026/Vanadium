#pragma once
/* TLS client built on the system Schannel SSP. No TLS protocol code lives here:
   Windows performs the handshake, we drive it, validate the peer certificate
   ourselves (chain policy plus host name) and expose a plain read/write stream.
   insecure_skip_verify exists only for the local test server and is wired to a
   test-only switch, never to the download command. */
#include <winsock2.h>

#include <string>

#include "vanadium/core.h"

namespace vd::remote::net {

struct TlsOptions {
  /* Test-only escape hatch. Production callers leave this false. */
  bool insecure_skip_verify = false;
  /* ALPN protocol list, one entry. Empty by default: the HTTP layer only
     speaks HTTP/1.1, which is also what a server picks when the client sends
     no ALPN extension, so no offer is needed (and a malformed offer makes
     some servers drop the connection during the handshake). */
  std::string alpn;
  int handshake_timeout_ms = 30000;
};

class TlsStream {
 public:
  /* Takes ownership of sock for the lifetime of the stream; the destructor
     closes it. host is used for SNI and certificate host name validation. */
  TlsStream(SOCKET sock, std::string host, TlsOptions options);
  ~TlsStream();
  TlsStream(const TlsStream&) = delete;
  TlsStream& operator=(const TlsStream&) = delete;

  void handshake();
  void write(const void* data, std::size_t len, int timeout_ms);
  /* Returns 0 once the peer sent close_notify. */
  std::size_t read(void* buf, std::size_t len, int timeout_ms);
  void shutdown();

  SOCKET socket() const { return sock_; }
  bool closed() const { return closed_; }

 private:
  void handshake_step(const void* initial_data, std::size_t initial_len);
  void verify_peer();
  void read_raw(int timeout_ms);

  SOCKET sock_;
  std::string host_;
  TlsOptions options_;
  void* ctx_ = nullptr;      /* PCtxtHandle */
  std::string pending_;      /* raw bytes received but not yet decrypted */
  std::string plain_;        /* decrypted bytes not yet handed to the caller */
  bool handshaken_ = false;
  bool closed_ = false;
  std::size_t header_size_ = 1024;   /* SECPKG_ATTR_STREAM_SIZES, queried in handshake */
  std::size_t trailer_size_ = 1024;
  std::size_t max_message_ = 16 * 1024;
};

/* Hex/name text for a SECURITY_STATUS value. */
std::string sspi_error_text(long status);

}  // namespace vd::remote::net
