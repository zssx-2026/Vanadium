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
#include <cstdio>
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


/* ---------- GitHub-like default pages ---------- */

/* One shared shell around every page: top bar with nav links. */
std::string page_shell(const std::string& title, const std::string& active,
                       const std::string& inner) {
  auto nav = [&](const char* href, const char* label) {
    std::string cls = (active == label) ? " class=\"on\"" : "";
    return "<a href=\"" + std::string(href) + "\"" + cls + ">" + label + "</a>";
  };
  std::string s = "<!doctype html><html><head><meta charset=\"utf-8\">";
  s += "<title>" + html_escape(title) + " - inv</title>";
  s += "<style>";
  s += "body{margin:0;font-family:Segoe UI,Arial,sans-serif;background:#0d1117;color:#e6edf3}";
  s += "header{background:#010409;border-bottom:1px solid #30363d;padding:0 24px;display:flex;align-items:center;gap:20px;height:56px}";
  s += "header .logo{color:#e6edf3;font-weight:700;text-decoration:none;font-size:18px}";
  s += "header a{color:#c9d1d9;text-decoration:none;padding:14px 8px;display:inline-block}";
  s += "header a.on{border-bottom:2px solid #a794ff}";
  s += "header a:hover{color:#ffffff}";
  s += "main{max-width:980px;margin:24px auto;padding:0 16px}";
  s += "h1{font-size:24px} h2{font-size:18px;border-bottom:1px solid #30363d;padding-bottom:8px}";
  s += "table{border-collapse:collapse;width:100%}";
  s += "td,th{border:1px solid #30363d;padding:6px 10px;text-align:left;font-size:14px}";
  s += "th{background:#161b22}";
  s += "a{color:#a794ff;text-decoration:none} a:hover{text-decoration:underline}";
  s += ".muted{color:#8b949e}";
  s += ".repohead{padding:16px 0;border-bottom:1px solid #30363d}";
  s += ".repotitle{font-size:20px;font-weight:600}";
  s += ".badge{display:inline-block;border:1px solid #30363d;border-radius:999px;padding:1px 10px;font-size:12px;color:#8b949e;margin-left:8px}";
  s += ".pills{display:flex;gap:8px;margin-top:12px}";
  s += ".pill{display:inline-flex;align-items:center;gap:6px;border:1px solid #30363d;border-radius:6px;background:#21262d;padding:4px 12px;font-size:12px;color:#e6edf3;cursor:default}";
  s += ".pill b{color:#8b949e;font-weight:500;background:#30363d;border-radius:999px;padding:0 6px}";
  s += ".tabs{display:flex;gap:4px;margin:16px 0 0;border-bottom:1px solid #30363d}";
  s += ".tab{padding:8px 14px;font-size:14px;color:#e6edf3;text-decoration:none;border-bottom:2px solid transparent}";
  s += ".tab.on{font-weight:600;border-bottom-color:#f78166}";
  s += ".tab:hover{color:#fff}";
  s += ".filelist{border:1px solid #30363d;border-radius:6px;margin-top:12px;overflow:hidden}";
  s += ".filelist table{border:none;width:100%}";
  s += ".filelist td{border:none;border-top:1px solid #21262d;padding:8px 16px;font-size:14px}";
  s += ".filelist tr:first-child td{border-top:none;background:#161b22;color:#8b949e;font-size:12px}";
  s += ".langbar{display:flex;height:8px;border-radius:999px;overflow:hidden;margin:8px 0}";
  s += ".langbar span{display:block;height:100%}";
  s += ".row{display:flex;gap:24px;align-items:flex-start}";
  s += ".col-main{flex:1;min-width:0}.col-side{width:260px;flex-shrink:0}";
  s += ".about h2{border:none;font-size:14px;margin-bottom:4px}";
  s += ".topic{display:inline-block;background:#1f6feb33;color:#79c0ff;border-radius:999px;padding:2px 10px;font-size:12px;margin:2px 4px 2px 0}";
  s += "</style></head><body>";
  s += "<header><a class=\"logo\" href=\"/\">inv</a>";
  s += nav("/", "Home") + nav("/releases", "Releases") + nav("/pull-req", "Pull requests");
  s += nav("/history", "History") + nav("/api", "API");
  s += "</header><main>";
  s += inner;
  s += "</main></body></html>";
  return s;
}

/* Repository page: GitHub-style header, tabs, file browser, About sidebar. */
std::string page_home(const Archive& a, const std::string& branch_path,
                      const std::vector<ServeEntry>& entries) {
  const auto branches = a.branches();
  const auto hist = a.history(branch_path, 100);
  u64 total_bytes = 0;
  for (const ServeEntry& e : entries) if (!e.dir) total_bytes += (u64)e.size;

  /* Language share by bytes per extension, top 5 (About sidebar). */
  std::map<std::string, u64> lang_bytes;
  for (const ServeEntry& e : entries) {
    if (e.dir) continue;
    const std::size_t dot = e.path.rfind('.');
    const std::string ext = dot == std::string::npos ? "(no ext)" : e.path.substr(dot + 1);
    lang_bytes[ext] += (u64)e.size;
  }
  std::vector<std::pair<std::string, u64> > langs(lang_bytes.begin(), lang_bytes.end());
  std::sort(langs.begin(), langs.end(),
            [](const std::pair<std::string, u64>& x, const std::pair<std::string, u64>& y)
            { return x.second > y.second; });

  const char* kColors[] = {"#a794ff", "#f78166", "#79c0ff", "#7ee787", "#ffa657"};
  std::string langbar, langlegend;
  u64 lang_total = 0;
  for (const auto& kv : langs) lang_total += kv.second;
  for (std::size_t i = 0; i < langs.size() && i < 5; ++i) {
    const double share = lang_total ? (100.0 * (double)langs[i].second / (double)lang_total) : 0.0;
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%.1f", share);
    langbar += "<span style=\"background:" + std::string(kColors[i]) + ";width:" +
               std::to_string((int)(share + 0.5)) + "%\"></span>";
    langlegend += "<span style=\"margin-right:14px\"><span style=\"color:" +
                  std::string(kColors[i]) + "\">&#9632;</span> " +
                  html_escape(langs[i].first) + " " + pct + "%</span>";
  }

  /* Repo header: name, Public badge, Watch/Fork/Star pills. */
  std::string inner;
  inner += "<div class=\"repohead\"><span class=\"repotitle\">" + html_escape(a.name()) +
           "</span><span class=\"badge\">Public</span>";
  inner += "<div class=\"pills\">";
  inner += "<span class=\"pill\">Watch <b>0</b></span>";
  inner += "<span class=\"pill\">Fork <b>" + std::to_string(branches.size() > 1 ? branches.size() - 1 : 0) +
           "</b></span>";
  inner += "<span class=\"pill\">Star <b>" + std::to_string(entries.size()) + "</b></span>";
  inner += "</div></div>";

  /* Repo tab bar: Code active, links into the other published pages. */
  auto tab = [](const char* href, const char* label, bool on) {
    return std::string("<a class=\"tab") + (on ? " on\"" : "\"" ) + " href=\"" + href + "\">" + label +
           "</a>";
  };
  inner += "<div class=\"tabs\">";
  inner += tab("/", "Code", true) + tab("/history", "History", false) +
           tab("/releases", "Releases", false) + tab("/pull-req", "Pull requests", false) +
           tab("/api", "API", false);
  inner += "</div>";

  /* Branch bar. */
  inner += "<p style=\"margin:14px 0 0\"><span class=\"badge\">" +
           html_escape(branch_path == "/" ? "main" : branch_path.substr(1)) + "</span> " +
           std::to_string(branches.size()) + " Branch" + (branches.size() == 1 ? "" : "es") +
           " &middot; " + std::to_string(hist.size()) + " Commit" + (hist.size() == 1 ? "" : "s") +
           " &middot; " + human_size(total_bytes) + "</p>";

  /* Latest commit strip. */
  if (!hist.empty()) {
    const Snapshot& h = hist.front();
    inner += "<div class=\"filelist\"><table><tr><td><code class=\"muted\">" +
             html_escape(h.id.substr(0, 10)) + "</code> &middot; " +
             html_escape(h.message.empty() ? "(no message)" : h.message) + " <span class=\"muted\">" +
             html_escape(iso_time(h.time_ms)) + "</span></td></tr></table></div>";
  }

  /* File browser + About sidebar. */
  inner += "<div class=\"row\"><div class=\"col-main\"><div class=\"filelist\"><table>";
  inner += "<tr><td>Name</td><td style=\"width:110px\">Size</td><td style=\"width:90px\">Type</td></tr>";
  std::vector<const ServeEntry*> dirs, files;
  for (const ServeEntry& e : entries) (e.dir ? dirs : files).push_back(&e);
  auto by_name = [](const ServeEntry* x, const ServeEntry* y) { return x->path < y->path; };
  std::sort(dirs.begin(), dirs.end(), by_name);
  std::sort(files.begin(), files.end(), by_name);
  for (const ServeEntry* e : dirs) {
    inner += "<tr><td><a href=\"/files/" + html_escape(e->path) + "/\">&#128193; " +
             html_escape(e->path.substr(e->path.rfind('/') + 1)) + "/</a></td><td>-</td><td>dir</td></tr>";
  }
  for (const ServeEntry* e : files) {
    inner += "<tr><td><a href=\"/files/" + html_escape(e->path) + "\">&#128196; " +
             html_escape(e->path.substr(e->path.rfind('/') + 1)) + "</a></td><td>" +
             human_size((u64)e->size) + "</td><td>file</td></tr>";
  }
  if (entries.empty()) inner += "<tr><td class=\"muted\">(empty snapshot)</td><td></td><td></td></tr>";
  inner += "</table></div></div>";

  inner += "<div class=\"col-side about\"><h2>About</h2>";
  inner += "<p class=\"muted\">Served from the inv archive <code>" + html_escape(a.id().substr(0, 12)) +
           "</code>.</p>";
  if (!langbar.empty()) {
    inner += "<h2 style=\"margin-top:18px\">Languages</h2><div class=\"langbar\">" + langbar +
             "</div><p style=\"font-size:12px\">" + langlegend + "</p>";
  }
  inner += "<h2 style=\"margin-top:18px\">Topics</h2>";
  inner += "<span class=\"topic\">inv</span><span class=\"topic\">version-control</span>";
  inner += "</div></div>";
  return page_shell(a.name(), "Home", inner);
}


std::string page_history(const Archive& a, const std::string& branch_path) {
  std::string inner = "<h1>History <span class=\"muted\">" + html_escape(branch_path) + "</span></h1>";
  const auto hist = a.history(branch_path, 100);
  inner += "<table><tr><th>Seq</th><th>Message</th><th>Time</th><th>Id</th></tr>";
  for (const auto& s : hist) {
    inner += "<tr><td>#" + std::to_string(s.seq) + "</td>";
    inner += "<td>" + html_escape(s.message.empty() ? "(no message)" : s.message) + "</td>";
    inner += "<td>" + html_escape(iso_time(s.time_ms)) + "</td>";
    inner += "<td class=\"muted\">" + html_escape(s.id.substr(0, 12)) + "</td></tr>";
  }
  inner += "</table>";
  return page_shell("History", "History", inner);
}

std::string page_releases(const Archive& a) {
  std::string inner = "<h1>Releases</h1>";
  inner += "<p class=\"muted\">Releases are published with <code>inv release</code>.</p>";
  return page_shell("Releases", "Releases", inner);
}

std::string page_pullreq() {
  std::string inner = "<h1>Pull requests</h1>";
  inner += "<p class=\"muted\">No open pull requests. Fork with <code>inv fork</code> and push back with <code>inv push</code>.</p>";
  return page_shell("Pull requests", "Pull requests", inner);
}

std::string page_api() {
  std::string inner = "<h1>API</h1>";
  inner += "<p>JSON endpoints:</p><table>";
  inner += "<tr><th>Endpoint</th><th>Description</th></tr>";
  inner += "<tr><td><a href=\"/api/heads\">/api/heads</a></td><td>branch heads</td></tr>";
  inner += "<tr><td><a href=\"/api/history\">/api/history</a></td><td>snapshot history</td></tr>";
  inner += "<tr><td><a href=\"/api/branches\">/api/branches</a></td><td>branch list</td></tr>";
  inner += "<tr><td><a href=\"/api/entries\">/api/entries</a></td><td>head snapshot entries</td></tr>";
  inner += "</table>";
  return page_shell("API", "API", inner);
}

std::string page_404(const std::string& target) {
  std::string inner = "<h1>404 - \xe9\xa1\xb5\xe9\x9d\xa2\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8</h1>";
  inner += "<p class=\"muted\">" + html_escape(target) + "</p>";
  return page_shell("Not found", "", inner);
}

std::string page_login(bool signup) {
  std::string inner = signup ? "<h1>Sign up</h1>" : "<h1>Sign in</h1>";
  inner += "<form><table>";
  inner += "<tr><td>Username</td><td><input name=\"user\"></td></tr>";
  inner += "<tr><td>Password</td><td><input name=\"pass\" type=\"password\"></td></tr>";
  inner += "<tr><td></td><td><button type=\"submit\">" + std::string(signup ? "Sign up" : "Sign in") + "</button></td></tr>";
  inner += "</table></form>";
  return page_shell(signup ? "Sign up" : "Sign in", "", inner);
}

std::string page_token() {
  std::string inner = "<h1>Token management</h1>";
  inner += "<p class=\"muted\">Tokens are managed with <code>inv login</code>; credentials never leave this machine.</p>";
  return page_shell("Tokens", "", inner);
}

std::string page_my(const Archive& a) {
  std::string inner = "<h1>My archive</h1>";
  inner += "<p>Name: " + html_escape(a.name()) + "</p>";
  inner += "<p>Id: <code>" + html_escape(a.id()) + "</code></p>";
  return page_shell("My", "", inner);
}

/* JSON responses for the /api endpoints. */
std::string api_json(const Archive& a, const std::string& branch_path,
                     const std::string& sub, const std::vector<ServeEntry>& entries) {
  Json j = Json::object();
  if (sub == "heads") {
    j.set("branch", branch_path);
    auto h = a.head(branch_path);
    if (h) {
      j.set("seq", h->seq);
      j.set("id", h->id);
      j.set("time_ms", h->time_ms);
      j.set("message", h->message);
    }
  } else if (sub == "history") {
    Json arr = Json::array();
    for (const auto& s : a.history(branch_path, 100)) {
      Json e = Json::object();
      e.set("seq", s.seq);
      e.set("id", s.id);
      e.set("time_ms", s.time_ms);
      e.set("message", s.message);
      arr.push(std::move(e));
    }
    j.set("history", std::move(arr));
  } else if (sub == "branches") {
    Json arr = Json::array();
    for (const auto& b : a.branches()) {
      Json e = Json::object();
      e.set("path", b.path);
      e.set("name", b.name);
      e.set("head_seq", b.head_seq);
      arr.push(std::move(e));
    }
    j.set("branches", std::move(arr));
  } else if (sub == "entries") {
    Json arr = Json::array();
    for (const ServeEntry& e : entries) {
      Json o = Json::object();
      o.set("path", e.path);
      o.set("dir", e.dir);
      o.set("size", e.size);
      o.set("sha", e.sha);
      arr.push(std::move(o));
    }
    j.set("entries", std::move(arr));
  } else {
    j = Json::object();
    j.set("error", "unknown endpoint");
  }
  return j.dump();
}

int cmd_serve_impl(App& app, const std::vector<std::string>& args) {
  remote::net::ensure_winsock();
  int port = 8341;
  /* Spec flags (m05011). -cli/-nocommand/-page/-whitelist/-whiteip land with the
     interactive command loop; here we honor the flags that shape this server loop. */
  bool detlog = has_flag(args, "-detlog");
  bool simlog = has_flag(args, "-simlog");
  bool nopage = has_flag(args, "-nopage");
  bool allwks = has_flag(args, "-allwks");
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

    /* Helper lambdas keep the routing linear. */
    auto respond = [&](int code, const std::string& b, const std::string& ct) {
      const char* reason = code == 200 ? "OK" : (code == 404 ? "Not Found" :
                            (code == 405 ? "Method Not Allowed" : "Internal Server Error"));
      std::string resp = "HTTP/1.1 " + std::to_string(code) + " " + reason +
                         "\r\nContent-Type: " + ct +
                         "\r\nContent-Length: " + std::to_string(b.size()) +
                         "\r\nConnection: close\r\n\r\n" + b;
      ::send(cs, resp.data(), (int)resp.size(), 0);
      closesocket(cs);
      if (detlog) {
        char ts[32];
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
        localtime_s(&tmv, &now);
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
        app.ui.line("[" + std::string(ts) + "][access] " + method + " " + target + " " +
                    std::to_string(code) + " " + std::to_string(b.size()) + "B");
      }
    };

    if (method != "GET" && method != "HEAD") {
      respond(405, "<h1>405 Method Not Allowed</h1>", ctype);
      continue;
    }
    if (target.empty()) target = "/";

    /* 1) /assets/<sha> raw object link (works in every mode but -nopage). */
    if (starts_with(target, "/assets/")) {
      const std::string sha = target.substr(8);
      std::string data;
      if (sha.size() >= 8 && a.objects().get(sha, data)) {
        ctype = "application/octet-stream";
        respond(200, data, ctype);
      } else {
        respond(404, page_404(target), ctype);
      }
      continue;
    }

    /* 2) /api/<name> JSON endpoints. */
    if (starts_with(target, "/api/")) {
      const std::string sub = target.substr(5);
      ctype = "application/json";
      if (sub == "branches" || sub == "history" || sub == "heads" || sub == "entries") {
        respond(200, api_json(a, branch_path, sub, entries), ctype);
      } else if (sub == "info") {
        Json j = Json::object();
        j.set("name", a.name());
        j.set("id", a.id());
        j.set("type", archive_type_name(a.type()));
        j.set("branch", branch_path);
        respond(200, j.dump(), ctype);
      } else {
        Json j = Json::object();
        j.set("error", "unknown endpoint");
        respond(404, j.dump(), ctype);
      }
      continue;
    }

    /* 3) file routes: /files/<path> direct, //path legacy, bare /<path> fallback. */
    std::string file_key;
    if (starts_with(target, "/files/")) {
      file_key = target.substr(6);              /* keep leading slash */
    } else if (target.size() >= 2 && target[0] == '/' && target[1] == '/') {
      file_key = target.substr(1);
    } else if (target != "/" && route.count(target)) {
      file_key = target;
    }
    if (!file_key.empty()) {
      while (file_key.size() > 1 && file_key.back() == '/') file_key.pop_back();
      if (file_key == "/") file_key.clear();     /* /files/ lists the root */
      if (file_key.empty()) {
        respond(200, page_home(a, branch_path, entries), ctype);
        continue;
      }
      auto it = route.find(file_key);
      if (it != route.end()) {
        std::string data;
        if (a.objects().get(it->second->sha, data)) {
          ctype = content_type_for(it->second->path);
          respond(200, data, ctype);
        } else {
          respond(500, "<h1>500 Object read failed</h1>", ctype);
        }
      } else {
        /* directory listing? collect direct children */
        const std::string prefix = file_key.substr(1) + "/";
        std::string listing = "<h1>/" + html_escape(file_key.substr(1)) + "</h1><table>";
        bool any = false;
        for (const ServeEntry& e : entries) {
          if (e.path.rfind(prefix, 0) == 0 &&
              e.path.find('/', prefix.size()) == std::string::npos) {
            any = true;
            listing += "<tr><td><a href=\"/files/" + html_escape(e.path) + (e.dir ? "/" : "") + "\">" +
                       html_escape(e.path.substr(prefix.size())) + (e.dir ? "/" : "") + "</a></td></tr>";
          }
        }
        listing += "</table>";
        if (any) respond(200, page_shell(file_key, "", listing), ctype);
        else respond(404, page_404(target), ctype);
      }
      continue;
    }

    /* 4) pages (skipped in -nopage mode). */
    if (nopage) {
      respond(404, "", ctype);                   /* -nopage: no pages at all */
      continue;
    }
    if (target == "/") {
      respond(200, page_home(a, branch_path, entries), ctype);
    } else if (target == "/releases") {
      respond(200, page_releases(a), ctype);
    } else if (target == "/pull-req") {
      respond(200, page_pullreq(), ctype);
    } else if (target == "/history") {
      respond(200, page_history(a, branch_path), ctype);
    } else if (target == "/api") {
      respond(200, page_api(), ctype);
    } else if (target == "/login") {
      respond(200, page_login(false), ctype);
    } else if (target == "/signup") {
      respond(200, page_login(true), ctype);
    } else if (target == "/token" || target == "/token/new") {
      respond(200, page_token(), ctype);
    } else if (target == "/my") {
      respond(200, page_my(a), ctype);
    } else if (starts_with(target, "/workspace")) {
      if (!allwks) {
        respond(404, page_404(target), ctype);
      } else {
        std::string inner = "<h1>Infinity CodeLab</h1>";
        if (target == "/workspace") {
          inner += "<p class=\"muted\">Workspaces are enabled (-allwks).</p>";
          inner += "<p><a href=\"/workspace/new\">New workspace</a> &middot; <a href=\"/workspace/s\">Existing workspaces</a></p>";
        } else {
          inner += "<p class=\"muted\">" + html_escape(target) + "</p>";
        }
        respond(200, page_shell("Infinity CodeLab", "", inner), ctype);
      }
    } else {
      respond(404, page_404(target), ctype);
    }
  }
  return 0;
}

}  // namespace

int cmd_serve(App& app, const std::vector<std::string>& args) {
  return cmd_serve_impl(app, args);
}

}  // namespace vd
