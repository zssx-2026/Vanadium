#include "socket.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>

/* mingw-w64 ships GetAddrInfoExW/FreeAddrInfoExW but omits the two Vista-era
   helpers from ws2tcpip.h even though libws2_32.a exports them. */
extern "C" {
WINSOCK_API_LINKAGE INT WSAAPI GetAddrInfoExCancel(LPHANDLE lpHandle);
WINSOCK_API_LINKAGE INT WSAAPI GetAddrInfoExOverlappedResult(LPOVERLAPPED lpOverlapped);
}

namespace vd::remote::net {
namespace {

using clock_type = std::chrono::steady_clock;

struct DnsEntry {
  clock_type::time_point expires;
  std::vector<Endpoint> addrs;
};

std::mutex g_dns_mutex;
std::map<std::string, DnsEntry> g_dns_cache;
constexpr int kDnsTtlSeconds = 60;

std::wstring widen(const std::string& s) {
  if (s.empty()) return std::wstring();
  int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(need), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), need);
  return out;
}

std::string trim_ws(const std::string& s) {
  std::size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return std::string();
  std::size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

bool wait_fd(SOCKET s, bool for_write, int timeout_ms) {
  if (timeout_ms < 0) timeout_ms = 0;
  fd_set set;
  FD_ZERO(&set);
  FD_SET(s, &set);
  timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  /* Windows reports a failed non-blocking connect through exceptfds, not by
     making the socket writable — a refusal must wake us, or every dead port
     burns its whole connect budget before surfacing the error. */
  fd_set* wset = for_write ? &set : nullptr;
  fd_set eset;
  FD_ZERO(&eset);
  FD_SET(s, &eset);
  int rc = ::select(0, for_write ? nullptr : &set, wset, &eset, &tv);
  if (rc == SOCKET_ERROR) throw_socket_error("select failed", WSAGetLastError());
  if (rc == 0) return false;
  if (!for_write) return true;
  if (FD_ISSET(s, &eset)) {
    /* Exceptional completion: the connect failed. Let the caller pick the
       error up from SO_ERROR and treat this like a writable wakeup that
       fails the SO_ERROR check. */
    return true;
  }
  return FD_ISSET(s, &set);
}

}  // namespace

void ensure_winsock() {
  static std::once_flag once;
  std::call_once(once, [] {
    WSADATA data{};
    int rc = WSAStartup(MAKEWORD(2, 2), &data);
    if (rc != 0) throw_socket_error("WSAStartup failed", rc);
  });
}

std::string socket_error_text(int code) {
  char* buffer = nullptr;
  DWORD n = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, static_cast<DWORD>(code), 0,
                           reinterpret_cast<char*>(&buffer), 0, nullptr);
  std::string text;
  if (n && buffer) text = trim_ws(std::string(buffer, n));
  if (buffer) LocalFree(buffer);
  if (text.empty()) text = "winsock error " + std::to_string(code);
  return text;
}

int transport_code_for_wsa(int wsa_code) {
  switch (wsa_code) {
    case WSAETIMEDOUT:
      return 12002;
    case WSAENETUNREACH:
    case WSAEHOSTUNREACH:
    case WSAEADDRNOTAVAIL:
      return 12029;
    case WSAECONNREFUSED:
      /* Nothing is listening: retrying within seconds cannot help. Surfaced as a
         distinct hard-fail code so neither the probe nor the stream retries it. */
      return 120079;
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case WSAENETRESET:
    case WSAEDISCON:
      return 12030;
    case WSAHOST_NOT_FOUND:
    case WSATRY_AGAIN:
    case WSANO_RECOVERY:
    case WSANO_DATA:
      return 12007;
    case WSAEACCES:
      return 12029;
    default:
      return 0;
  }
}

void throw_socket_error(const std::string& what, int wsa_code, const std::string& context) {
  int mapped = transport_code_for_wsa(wsa_code);
  std::string detail = context.empty() ? std::string() : context + ": ";
  detail += socket_error_text(wsa_code);
  detail += " (" + std::to_string(mapped ? mapped : wsa_code) + ")";
  throw Failure(1, what, detail);
}

std::vector<Endpoint> resolve_host(const std::string& host, int port, int timeout_ms) {
  ensure_winsock();
  const std::string key = host + ":" + std::to_string(port);
  {
    std::lock_guard<std::mutex> lock(g_dns_mutex);
    auto it = g_dns_cache.find(key);
    if (it != g_dns_cache.end() && it->second.expires > clock_type::now()) {
      return it->second.addrs;
    }
  }

  std::wstring whost = widen(host);
  std::wstring wport = std::to_wstring(port);
  ADDRINFOEXW hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  ADDRINFOEXW* result = nullptr;
  HANDLE name_handle = nullptr;
  OVERLAPPED ov{};
  ov.hEvent = WSACreateEvent();
  if (!ov.hEvent) throw_socket_error("WSACreateEvent failed", WSAGetLastError());

  int rc = GetAddrInfoExW(whost.c_str(), wport.c_str(), NS_ALL, nullptr, &hints, &result,
                          nullptr, &ov, nullptr, &name_handle);
  if (rc == WSA_IO_PENDING) {
    DWORD wait = WaitForSingleObject(ov.hEvent, static_cast<DWORD>(std::max(0, timeout_ms)));
    if (wait == WAIT_TIMEOUT) {
      GetAddrInfoExCancel(&name_handle);
      WaitForSingleObject(ov.hEvent, 5000);
      CloseHandle(ov.hEvent);
      if (result) FreeAddrInfoExW(result);
      throw Failure(1, "dns lookup failed",
                    "resolve " + key + " timed out after " + std::to_string(timeout_ms) +
                        " ms (12002)");
    }
    rc = GetAddrInfoExOverlappedResult(&ov);
  }
  CloseHandle(ov.hEvent);
  if (rc != 0) {
    if (result) FreeAddrInfoExW(result);
    int mapped = transport_code_for_wsa(rc);
    if (!mapped) mapped = 12007;
    std::string detail = "resolve " + key + " failed: " + socket_error_text(rc) + " (" +
                         std::to_string(mapped) + ")";
    throw Failure(1, "dns lookup failed", detail);
  }

  std::vector<Endpoint> addrs;
  for (ADDRINFOEXW* it = result; it; it = it->ai_next) {
    if (!it->ai_addr || it->ai_addrlen == 0) continue;
    if (it->ai_addr->sa_family != AF_INET && it->ai_addr->sa_family != AF_INET6) continue;
    Endpoint ep;
    std::memcpy(&ep.addr, it->ai_addr, it->ai_addrlen);
    ep.len = static_cast<int>(it->ai_addrlen);
    ep.family = it->ai_addr->sa_family;
    addrs.push_back(ep);
  }
  if (result) FreeAddrInfoExW(result);
  if (addrs.empty()) {
    throw Failure(1, "dns lookup failed", "resolve " + key + " returned no address (12007)");
  }
  /* Prefer IPv4 first: the deployment targets are dual stack and some peers
     have broken IPv6 routes, so v4 gives a faster first connection. */
  std::stable_partition(addrs.begin(), addrs.end(),
                        [](const Endpoint& e) { return e.family == AF_INET; });

  {
    std::lock_guard<std::mutex> lock(g_dns_mutex);
    DnsEntry entry;
    entry.expires = clock_type::now() + std::chrono::seconds(kDnsTtlSeconds);
    entry.addrs = addrs;
    g_dns_cache[key] = entry;
  }
  return addrs;
}

SOCKET tcp_connect(const std::string& host, int port, int connect_ms) {
  ensure_winsock();
  std::vector<Endpoint> addrs = resolve_host(host, port, std::max(1000, connect_ms));
  auto deadline = clock_type::now() + std::chrono::milliseconds(std::max(0, connect_ms));
  std::string last_error = "no address attempted";
  int last_wsa = 0; /* real failure from the final attempt, not a synthesized timeout */

  for (const Endpoint& ep : addrs) {
    SOCKET s = ::socket(ep.family, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
      last_wsa = WSAGetLastError();
      last_error = socket_error_text(last_wsa);
      continue;
    }
    u_long nonblocking = 1;
    if (ioctlsocket(s, FIONBIO, &nonblocking) != 0) {
      last_wsa = WSAGetLastError();
      last_error = socket_error_text(last_wsa);
      close_socket(s);
      continue;
    }
    int rc = ::connect(s, reinterpret_cast<const sockaddr*>(&ep.addr), ep.len);
    if (rc == SOCKET_ERROR) {
      int err = WSAGetLastError();
      if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
        last_wsa = err;
        last_error = socket_error_text(err);
        close_socket(s);
        continue;
      }
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                           deadline - clock_type::now())
                           .count();
      if (remaining <= 0 || !wait_fd(s, true, static_cast<int>(remaining))) {
        last_error = "connect timed out";
        close_socket(s);
        continue;
      }
      int so_error = 0;
      int len = sizeof(so_error);
      if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len) != 0) {
        last_wsa = WSAGetLastError();
        last_error = socket_error_text(last_wsa);
        close_socket(s);
        continue;
      }
      if (so_error != 0) {
        last_wsa = so_error;
        last_error = socket_error_text(so_error);
        close_socket(s);
        continue;
      }
    }
    nonblocking = 0;
    ioctlsocket(s, FIONBIO, &nonblocking);
    set_nodelay(s);
    return s;
  }
  int mapped = transport_code_for_wsa(last_wsa != 0 ? last_wsa : WSAETIMEDOUT);
  if (last_wsa == 0 && last_error.find("timed out") == std::string::npos) mapped = 12029;
  throw Failure(1, "connect failed",
                host + ":" + std::to_string(port) + " " + last_error + " (" +
                    std::to_string(mapped) + ")");
}

void set_nodelay(SOCKET s) {
  BOOL on = TRUE;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
}

void close_socket(SOCKET s) {
  if (s != INVALID_SOCKET) closesocket(s);
}

void send_all(SOCKET s, const void* data, std::size_t len, int timeout_ms,
              const std::string& context) {
  const char* p = static_cast<const char*>(data);
  std::size_t sent = 0;
  while (sent < len) {
    int chunk = static_cast<int>(std::min<std::size_t>(len - sent, 1u << 20));
    int rc = ::send(s, p + sent, chunk, 0);
    if (rc == SOCKET_ERROR) {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK) {
        if (!wait_fd(s, true, timeout_ms)) {
          throw Failure(1, "send failed",
                        (context.empty() ? std::string() : context + ": ") +
                            "socket write timed out (12002)");
        }
        continue;
      }
      throw_socket_error("send failed", err, context);
    }
    sent += static_cast<std::size_t>(rc);
  }
}

std::size_t recv_some(SOCKET s, void* buf, std::size_t len, int timeout_ms,
                       const std::string& context) {
  for (;;) {
    int rc = ::recv(s, static_cast<char*>(buf), static_cast<int>(std::min<std::size_t>(len, 1u << 20)), 0);
    if (rc > 0) return static_cast<std::size_t>(rc);
    if (rc == 0) return 0;
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) {
      if (!wait_fd(s, false, timeout_ms)) {
        throw Failure(1, "recv failed",
                      (context.empty() ? std::string() : context + ": ") +
                          "socket read timed out (12002)");
      }
      continue;
    }
    if (err == WSAEINTR) continue;
    throw_socket_error("recv failed", err, context);
  }
}

}  // namespace vd::remote::net
