#pragma once
/* Raw TCP transport: Winsock initialisation, DNS with a timeout and a short
   lived cache, non-blocking connect, and deadline-bounded send/recv.
   Everything here throws vd::Failure; transport level error numbers follow the
   historical transport-layer codes (12002 timeout, 12029 cannot connect, 12030
   connection aborted, 12007 name not resolved) so the existing retry policy in
   download.cpp keeps classifying failures the same way. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 /* GetAddrInfoExCancel / GetAddrInfoExOverlappedResult */
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>

#include "vanadium/core.h"

namespace vd::remote::net {

/* WSAStartup once per process; safe to call from several threads. */
void ensure_winsock();

struct Endpoint {
  sockaddr_storage addr{};
  int len = 0;
  int family = 0;
};

/* Resolves host:port (service given numerically). Results are cached for 60 s
   per host/port. Throws Failure with code 12007 when the name does not
   resolve, 12002 when the lookup exceeds timeout_ms. */
std::vector<Endpoint> resolve_host(const std::string& host, int port, int timeout_ms);

/* Non-blocking connect with a deadline, trying every resolved address in turn.
   Throws 12002 on timeout and 12029 when no address could be reached. */
SOCKET tcp_connect(const std::string& host, int port, int connect_ms);

/* Sends the whole buffer, waiting for writability under timeout_ms per wait. */
void send_all(SOCKET s, const void* data, std::size_t len, int timeout_ms,
              const std::string& context = std::string());

/* Reads at least one byte, returning 0 for an orderly shutdown by the peer.
   Waits at most timeout_ms for data to become available. */
std::size_t recv_some(SOCKET s, void* buf, std::size_t len, int timeout_ms,
                       const std::string& context = std::string());

void set_nodelay(SOCKET s);
void close_socket(SOCKET s);

/* Human readable text for a Winsock error code, trimmed. */
std::string socket_error_text(int code);

/* Transport error number for a Winsock error, or 0 when it has no mapping. */
int transport_code_for_wsa(int wsa_code);

/* Throws Failure(1, what, "<detail> (<transport code>)"). */
[[noreturn]] void throw_socket_error(const std::string& what, int wsa_code,
                                     const std::string& context = std::string());

}  // namespace vd::remote::net
