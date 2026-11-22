#include "uri.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include "vanadium/core.h"

namespace vd::remote::net {
namespace {

bool is_scheme_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.';
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string percent_normalize(const std::string& s) {
  /* Keep the escape sequences as sent; only upper-case the hex digits so two
     spellings of the same resource share one pool key. */
  std::string out = s;
  for (std::size_t i = 0; i + 2 < out.size(); ++i) {
    if (out[i] != '%') continue;
    out[i + 1] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i + 1])));
    out[i + 2] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i + 2])));
    i += 2;
  }
  return out;
}

int default_port(const std::string& scheme) { return scheme == "https" ? 443 : 80; }

/* Removes "." and ".." segments per RFC 3986 5.2.4, keeping a trailing slash. */
std::string remove_dot_segments(const std::string& path) {
  std::vector<std::string> out;
  std::size_t i = 0;
  bool trailing = false;
  while (i <= path.size()) {
    std::size_t slash = path.find('/', i);
    if (slash == std::string::npos) slash = path.size();
    std::string seg = path.substr(i, slash - i);
    trailing = slash == path.size();
    if (seg == ".") {
      /* drop */
    } else if (seg == "..") {
      if (!out.empty()) out.pop_back();
    } else if (!seg.empty() || (out.empty() && i == 0)) {
      out.push_back(seg);
    }
    if (slash == path.size()) break;
    i = slash + 1;
  }
  std::string result;
  for (std::size_t k = 0; k < out.size(); ++k) {
    if (k) result.push_back('/');
    result += out[k];
  }
  if (result.empty()) return "/";
  if (trailing && result.back() != '/') result.push_back('/');
  if (result.front() != '/') result.insert(result.begin(), '/');
  return result;
}

}  // namespace

std::string Uri::authority() const {
  std::string out;
  if (is_ipv6()) {
    out = "[" + host + "]";
  } else {
    out = host;
  }
  if (port != default_port(scheme)) out += ":" + std::to_string(port);
  return out;
}

std::string Uri::target() const {
  std::string out = path.empty() ? "/" : path;
  if (!query.empty()) out += "?" + query;
  return out;
}

std::string Uri::to_string() const {
  std::string out = scheme + "://";
  if (!userinfo.empty()) out += userinfo + "@";
  out += authority();
  out += target();
  if (!fragment.empty()) out += "#" + fragment;
  return out;
}

Uri parse_uri(const std::string& url) {
  Uri u;
  std::size_t sep = url.find("://");
  if (sep == std::string::npos) {
    throw Failure(1, "invalid URL", "missing scheme in \"" + url + "\"");
  }
  u.scheme = lower(url.substr(0, sep));
  if (u.scheme != "http" && u.scheme != "https") {
    throw Failure(1, "invalid URL", "unsupported scheme \"" + u.scheme + "\"");
  }
  for (char c : u.scheme) {
    if (!is_scheme_char(c)) throw Failure(1, "invalid URL", "bad scheme in \"" + url + "\"");
  }
  std::size_t pos = sep + 3;
  std::size_t end = url.find_first_of("/?#", pos);
  if (end == std::string::npos) end = url.size();
  std::string authority = url.substr(pos, end - pos);
  if (authority.empty()) throw Failure(1, "invalid URL", "empty host in \"" + url + "\"");

  std::size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    u.userinfo = authority.substr(0, at);
    authority = authority.substr(at + 1);
  }

  std::string host;
  std::string port_text;
  if (!authority.empty() && authority.front() == '[') {
    std::size_t close = authority.find(']');
    if (close == std::string::npos) {
      throw Failure(1, "invalid URL", "unterminated IPv6 literal in \"" + url + "\"");
    }
    host = authority.substr(1, close - 1);
    if (close + 1 < authority.size()) {
      if (authority[close + 1] != ':') {
        throw Failure(1, "invalid URL", "bad IPv6 authority in \"" + url + "\"");
      }
      port_text = authority.substr(close + 2);
    }
  } else {
    std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(':') == colon) {
      host = authority.substr(0, colon);
      port_text = authority.substr(colon + 1);
    } else {
      host = authority;
    }
  }
  if (host.empty()) throw Failure(1, "invalid URL", "empty host in \"" + url + "\"");
  u.host = lower(host);
  if (!port_text.empty()) {
    int value = 0;
    for (char c : port_text) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        throw Failure(1, "invalid URL", "bad port in \"" + url + "\"");
      }
      value = value * 10 + (c - '0');
      if (value > 65535) throw Failure(1, "invalid URL", "port out of range in \"" + url + "\"");
    }
    u.port = value;
  } else {
    u.port = default_port(u.scheme);
  }

  std::string rest = url.substr(end);
  std::size_t hash = rest.find('#');
  if (hash != std::string::npos) {
    u.fragment = rest.substr(hash + 1);
    rest = rest.substr(0, hash);
  }
  std::size_t question = rest.find('?');
  if (question != std::string::npos) {
    u.query = rest.substr(question + 1);
    rest = rest.substr(0, question);
  }
  u.path = rest.empty() ? "/" : rest;
  if (u.path.front() != '/') u.path.insert(u.path.begin(), '/');
  u.path = percent_normalize(u.path);
  return u;
}

std::string resolve_reference(const std::string& base, const std::string& ref) {
  if (ref.empty()) return base;
  /* Absolute reference: has a scheme followed by ':'. */
  std::size_t colon = ref.find(':');
  if (colon != std::string::npos && colon > 0) {
    bool scheme_like = true;
    for (std::size_t i = 0; i < colon; ++i) {
      char c = ref[i];
      if (!std::isalpha(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.') {
        scheme_like = false;
        break;
      }
    }
    if (scheme_like) {
      std::string scheme = lower(ref.substr(0, colon));
      if (scheme == "http" || scheme == "https") return ref;
    }
  }
  Uri b = parse_uri(base);
  if (ref.rfind("//", 0) == 0) {
    return b.scheme + ":" + ref;
  }
  if (ref.front() == '#') {
    std::string out = base;
    std::size_t hash = out.find('#');
    if (hash != std::string::npos) out = out.substr(0, hash);
    return out + ref;
  }
  if (ref.front() == '?') {
    std::string out = b.scheme + "://" + b.authority() + b.path + ref;
    return out;
  }
  std::string path_part = ref;
  std::string tail;
  std::size_t cut = ref.find_first_of("?#");
  if (cut != std::string::npos) {
    path_part = ref.substr(0, cut);
    tail = ref.substr(cut);
  }
  std::string merged;
  if (path_part.empty()) {
    merged = b.path;
    if (tail.empty() && !b.query.empty()) tail = "?" + b.query;
  } else if (path_part.front() == '/') {
    merged = path_part;
  } else {
    std::string dir = b.path;
    std::size_t slash = dir.rfind('/');
    if (slash != std::string::npos) {
      dir = dir.substr(0, slash + 1);
    } else {
      dir = "/";
    }
    merged = dir + path_part;
  }
  std::string resolved = remove_dot_segments(merged);
  if (resolved.empty()) resolved = "/";
  return b.scheme + "://" + b.authority() + resolved + tail;
}

}  // namespace vd::remote::net
