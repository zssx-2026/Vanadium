/* inv serve: publish an archive branch over HTTP on the native transport.
   Spec (D:/丁陈子豪/输入.md): "Start server." animation, "Server is started.",
   "Start website:" with the LAN addresses, a "Website tree:" listing and
   Ctrl+C -> "Operation cancelled, exiting." + "saving data..." + "data is saved."
   (the Ctrl+C part is rendered by the shell's interrupt handler). */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "vanadium/commands.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/gen.h"
#include "vanadium/store.h"
#include "../core/remote/net/socket.h"

namespace vd {

namespace {

/* One published entry: path inside the branch snapshot plus its size. */
struct ServeEntry {
  std::string path;
  std::string sha;
  bool dir = false;
  i64 size = 0;
};

std::string html_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

/* Minimal content types for common served extensions. */
std::string content_type_for(const std::string& path) {
  static const struct { const char* ext; const char* type; } kTypes[] = {
    {".html", "text/html; charset=utf-8"}, {".htm", "text/html; charset=utf-8"},
    {".css",  "text/css; charset=utf-8"},  {".js",   "text/javascript; charset=utf-8"},
    {".json", "application/json; charset=utf-8"},
    {".txt",  "text/plain; charset=utf-8"}, {".md",  "text/plain; charset=utf-8"},
    {".xml",  "application/xml"}, {".csv",  "text/csv"},
    {".png",  "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
    {".gif",  "image/gif"}, {".svg", "image/svg+xml"}, {".ico", "image/x-icon"},
    {".pdf",  "application/pdf"}, {".zip", "application/zip"},
  };
  for (const auto& t : kTypes) {
    const std::size_t n = std::strlen(t.ext);
    if (path.size() >= n && path.compare(path.size() - n, n, t.ext) == 0) return t.type;
  }
  return "application/octet-stream";
}

/* Local IP addresses for the "Start website:" block. */
std::vector<std::string> local_addresses() {
  std::vector<std::string> out;
  char host[256] = {};
  if (gethostname(host, sizeof(host) - 1) != 0) return out;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host, nullptr, &hints, &res) != 0) return out;
  for (addrinfo* p = res; p; p = p->ai_next) {
    char ip[64] = {};
    if (getnameinfo(p->ai_addr, (socklen_t)p->ai_addrlen, ip, sizeof(ip), nullptr, 0, NI_NUMERICHOST) == 0) {
      std::string s = ip;
      /* Skip the loopback: the spec lists reachable addresses. */
      if (s.rfind("127.", 0) == 0 || s == "::1") continue;
      if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    }
  }
  freeaddrinfo(res);
  return out;
}

int cmd_serve_impl(App& app, const std::vector<std::string>& args) {
  remote::net::ensure_winsock();
  int port = 8341;
  /* Spec flags (m05011). -cli/-nocommand/-page/-whitelist/-whiteip land with the
     interactive command loop; here we honor the flags that shape this server loop. */
  bool detlog = has_flag(args, "-detlog");
  bool simlog = has_flag(args, "-simlog");
  bool nopage = has_flag(args, "-nopage");
  bool lockdata = has_flag(args, "-lockdata");
  bool automd = has_flag(args, "-automd");
  bool autoagree = has_flag(args, "-autoagree");
  long maxm = 0, minm = 0;
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == "-port") port = std::atoi(args[i + 1].c_str());
    else if (args[i] == "-maxm") maxm = std::atol(args[i + 1].c_str());
    else if (args[i] == "-minm") minm = std::atol(args[i + 1].c_str());
  }
  if (maxm > 0 && maxm < minm) { fail(4103, "-maxm must be >= -minm"); }
  (void)simlog; (void)lockdata; (void)automd; (void)autoagree;
  /* Walk up from the working directory to the archive root (meta.json or
     legacy invar.json), mirroring open_here() in commands.cpp. */
  std::string base = fs::absolute(app.cwd);
  std::string aroot;
  while (!base.empty()) {
    if (fs::is_file(fs::join(base, "meta.json")) || fs::is_file(fs::join(base, "invar.json"))) { aroot = base; break; }
    const std::size_t pos = base.find_last_of("/\\");
    if (pos == std::string::npos || pos == 0) break;
    base = base.substr(0, pos);
  }
  if (aroot.empty()) fail(3100, msg("store.not_archive"), fs::absolute(app.cwd));
  Archive a = Archive::open(aroot);

  /* Spec animation: the shell renders the dots on the status line; the kernel
     emits the base text and then the settled line. */
  app.ui.status("serve.start", "Start server");
  app.ui.clear_status();
  app.ui.line("Server is started.");

  /* Listener: blocking accept loop on a dedicated socket. */
  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) fail(4100, "cannot create listener socket");
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<u16>(port));
  int reuse = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
  if (bind(listener, (sockaddr*)&addr, sizeof(addr)) != 0) {
    int code = WSAGetLastError();
    closesocket(listener);
    fail(4101, "cannot bind port " + std::to_string(port) + " (error " + std::to_string(code) + ")");
  }
  if (listen(listener, 8) != 0) {
    closesocket(listener);
    fail(4102, "cannot listen on port " + std::to_string(port));
  }

  /* "Start website:" block with every non-loopback local address. */
  app.ui.line("Start website:");
  for (const std::string& ip : local_addresses())
    app.ui.line("  http://" + ip + ":" + std::to_string(port) + "/");

  /* "Website tree:" from the published branch snapshot. */
  const std::string branch = opt_value(args, "-branch", std::string());
  const std::string branch_path = branch.empty() ? "/" : gen::normalize_path(branch);
  auto head = a.head(branch_path);
  std::vector<ServeEntry> entries;
  if (head) {
    for (const auto& e : head->entries) {
      ServeEntry se;
      se.path = e.path;
      se.sha = e.sha;
      se.dir = e.dir;
      se.size = (i64)e.size;
      entries.push_back(se);
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const ServeEntry& x, const ServeEntry& y) { return x.path < y.path; });
  app.ui.line("Website tree:");
  app.ui.line("/");
  for (const ServeEntry& e : entries) app.ui.line("  " + e.path + (e.dir ? "/" : ""));

  /* Map path -> entry for request routing. */
  std::map<std::string, const ServeEntry*> route;
  for (const ServeEntry& e : entries) route["/" + e.path] = &e;

  /* Serve loop. Ctrl+C arrives as the shell's interrupt handler closing the
     process; the socket close is best effort. */
  for (;;) {
    sockaddr_in peer{};
    int peer_len = sizeof(peer);
    SOCKET cs = accept(listener, (sockaddr*)&peer, &peer_len);
    if (cs == INVALID_SOCKET) continue;

    /* Read the request head (request line + headers, up to 8 KiB). */
    std::string req;
    char buf[4096];
    for (;;) {
      int n = ::recv(cs, buf, sizeof(buf), 0);
      if (n <= 0) break;
      req.append(buf, (std::size_t)n);
      if (req.find("\r\n\r\n") != std::string::npos || req.size() > 8192) break;
    }
    std::string method = "GET", target = "/";
    {
      std::istringstream is(req);
      is >> method >> target;
    }
    const std::size_t qpos = target.find('?');
    if (qpos != std::string::npos) target = target.substr(0, qpos);

    std::string body;
    std::string ctype = "text/html; charset=utf-8";
    if (method != "GET" && method != "HEAD") {
      body = "<h1>405 Method Not Allowed</h1>";
      std::string resp = "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: " + ctype +
                         "\r\nContent-Length: " + std::to_string(body.size()) +
                         "\r\nConnection: close\r\n\r\n" + body;
      ::send(cs, resp.data(), (int)resp.size(), 0);
      closesocket(cs);
      continue;
    }
    if (nopage && (target == "/" || target.empty())) {
      /* -nopage: 无页面模式 — no directory listing pages. */
      std::string resp404 = "HTTP/1.1 404 Not Found\r\nContent-Type: " + ctype +
                            "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      ::send(cs, resp404.data(), (int)resp404.size(), 0);
      closesocket(cs);
      continue;
    }
    if (target == "/" || target.empty()) {
      body = "<html><head><title>inv</title></head><body><h1>/</h1><ul>";
      for (const ServeEntry& e : entries)
        body += "<li><a href=\"/" + html_escape(e.path) + (e.dir ? "/" : "") + "\">" +
                html_escape(e.path) + (e.dir ? "/" : "") + "</a></li>";
      body += "</ul></body></html>";
    } else {
      std::string key = target;
      while (key.size() > 1 && key.back() == '/') key.pop_back();
      auto it = route.find(key);
      if (it == route.end()) {
        body = "<html><body><h1>404 Not Found</h1></body></html>";
        std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Type: " + ctype +
                           "\r\nContent-Length: " + std::to_string(body.size()) +
                           "\r\nConnection: close\r\n\r\n" + body;
        ::send(cs, resp.data(), (int)resp.size(), 0);
        closesocket(cs);
        continue;
      }
      if (it->second->dir) {
        body = "<html><head><title>inv</title></head><body><h1>" + html_escape(key) +
               "</h1><ul>";
        for (const ServeEntry& e : entries) {
          if (e.path.rfind(it->second->path + "/", 0) == 0 &&
              e.path.find('/', it->second->path.size() + 1) == std::string::npos)
            body += "<li><a href=\"/" + html_escape(e.path) + (e.dir ? "/" : "") + "\">" +
                    html_escape(e.path.substr(it->second->path.size() + 1)) + (e.dir ? "/" : "") + "</a></li>";
        }
        body += "</ul></body></html>";
      } else {
        /* Read file bytes from the objects store. */
        std::string data;
        if (a.objects().get(it->second->sha, data)) {
          body = data;
          ctype = content_type_for(it->second->path);
        } else {
          body = "<html><body><h1>500 Object read failed</h1></body></html>";
          std::string resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: " + ctype +
                             "\r\nContent-Length: " + std::to_string(body.size()) +
                             "\r\nConnection: close\r\n\r\n" + body;
          ::send(cs, resp.data(), (int)resp.size(), 0);
          closesocket(cs);
          continue;
        }
      }
    }
    std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: " + ctype +
                       "\r\nContent-Length: " + std::to_string(body.size()) +
                       "\r\nConnection: close\r\n\r\n" + body;
    ::send(cs, resp.data(), (int)resp.size(), 0);
    closesocket(cs);
    /* -detlog: timestamped access line per request (spec [<时间戳>][<type>]). */
    if (detlog) {
      char ts[32];
      std::time_t now = std::time(nullptr);
      std::tm tmv{};
      localtime_s(&tmv, &now);
      std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
      app.ui.line("[" + std::string(ts) + "][access] " + method + " " + target +
                  " 200 " + std::to_string(body.size()) + "B");
    }
  }
  return 0;
}

}  // namespace

int cmd_serve(App& app, const std::vector<std::string>& args) {
  return cmd_serve_impl(app, args);
}

}  // namespace vd
