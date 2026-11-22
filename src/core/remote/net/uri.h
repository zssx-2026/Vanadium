#pragma once
/* URL parsing and reference resolution for the self-contained HTTP stack.
   Only http and https are accepted: the transport speaks TLS through Schannel,
   everything else is rejected before a socket is opened. */
#include <string>

namespace vd::remote::net {

struct Uri {
  std::string scheme;   /* "http" or "https", lower case */
  std::string userinfo; /* optional "user:pass", kept verbatim (unused by HTTP) */
  std::string host;     /* lower case; IPv6 literals keep no brackets */
  int port = 0;         /* explicit port, or the scheme default */
  std::string path = "/";
  std::string query;    /* without the leading '?' */
  std::string fragment; /* without the leading '#' */

  bool secure() const { return scheme == "https"; }
  bool is_ipv6() const { return host.find(':') != std::string::npos; }
  /* host[:port], bracketing IPv6 literals. */
  std::string authority() const;
  /* Origin-form request target: path plus "?query". */
  std::string target() const;
  std::string to_string() const;
};

/* Throws vd::Failure(1, "invalid URL", ...) when the URL is not absolute
   http(s) or the authority is malformed. */
Uri parse_uri(const std::string& url);

/* RFC 3986 section 5.2 reference resolution against an absolute base URL.
   Handles absolute URLs, scheme-relative "//host/x", absolute paths, relative
   paths, query-only and fragment-only references, with dot-segment removal. */
std::string resolve_reference(const std::string& base, const std::string& ref);

}  // namespace vd::remote::net
