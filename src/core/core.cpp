#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <sstream>

#include "vanadium/core.h"

namespace vd {

void fail(int code, const std::string& message, const std::string& detail) {
  throw Failure(code, message, detail);
}

/* ------------------------------------------------------------------ Json */

Json::Json() = default;
Json::Json(std::nullptr_t) : type_(Type::Null) {}
Json::Json(bool v) : type_(Type::Bool), b_(v) {}
Json::Json(int v) : type_(Type::Int), i_(v) {}
Json::Json(i64 v) : type_(Type::Int), i_(v) {}
Json::Json(double v) : type_(Type::Double), d_(v) {}
Json::Json(const char* v) : type_(Type::String), s_(v ? v : "") {}
Json::Json(std::string v) : type_(Type::String), s_(std::move(v)) {}

Json Json::array() {
  Json j;
  j.type_ = Type::Array;
  return j;
}
Json Json::object() {
  Json j;
  j.type_ = Type::Object;
  return j;
}

bool Json::as_bool(bool def) const {
  if (type_ == Type::Bool) return b_;
  if (type_ == Type::Int) return i_ != 0;
  return def;
}
i64 Json::as_int(i64 def) const {
  if (type_ == Type::Int) return i_;
  if (type_ == Type::Double) return static_cast<i64>(d_);
  if (type_ == Type::Bool) return b_ ? 1 : 0;
  if (type_ == Type::String) {
    try {
      return std::stoll(s_);
    } catch (...) {
      return def;
    }
  }
  return def;
}
double Json::as_double(double def) const {
  if (type_ == Type::Double) return d_;
  if (type_ == Type::Int) return static_cast<double>(i_);
  if (type_ == Type::String) {
    try {
      return std::stod(s_);
    } catch (...) {
      return def;
    }
  }
  return def;
}
const std::string& Json::as_string() const {
  static const std::string kEmpty;
  return type_ == Type::String ? s_ : kEmpty;
}
std::string Json::as_string_or(const std::string& def) const {
  return type_ == Type::String ? s_ : def;
}

Json& Json::operator[](const std::string& key) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    obj_.clear();
  }
  for (auto& kv : obj_)
    if (kv.first == key) return kv.second;
  obj_.emplace_back(key, Json());
  return obj_.back().second;
}
const Json& Json::operator[](const std::string& key) const {
  static const Json kNull;
  for (const auto& kv : obj_)
    if (kv.first == key) return kv.second;
  return kNull;
}
Json& Json::operator[](std::size_t idx) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    arr_.clear();
  }
  if (idx >= arr_.size()) arr_.resize(idx + 1);
  return arr_[idx];
}
const Json& Json::operator[](std::size_t idx) const {
  static const Json kNull;
  return idx < arr_.size() ? arr_[idx] : kNull;
}
void Json::push(Json v) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    arr_.clear();
  }
  arr_.push_back(std::move(v));
}
void Json::set(const std::string& key, Json v) { (*this)[key] = std::move(v); }
bool Json::contains(const std::string& key) const {
  for (const auto& kv : obj_)
    if (kv.first == key) return true;
  return false;
}
std::size_t Json::size() const {
  if (type_ == Type::Array) return arr_.size();
  if (type_ == Type::Object) return obj_.size();
  if (type_ == Type::String) return s_.size();
  return 0;
}
std::vector<std::string> Json::keys() const {
  std::vector<std::string> out;
  for (const auto& kv : obj_) out.push_back(kv.first);
  return out;
}
void Json::erase(const std::string& key) {
  obj_.erase(std::remove_if(obj_.begin(), obj_.end(),
                            [&](const std::pair<std::string, Json>& kv) {
                              return kv.first == key;
                            }),
             obj_.end());
}

namespace {

void escape_json(const std::string& s, std::string& out) {
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

void number_to_string(double d, std::string& out) {
  char buf[64];
  if (d == static_cast<double>(static_cast<i64>(d)) && d < 9.2e18 && d > -9.2e18) {
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
  } else {
    std::snprintf(buf, sizeof(buf), "%.17g", d);
  }
  out += buf;
}

struct Parser {
  const std::string& text;
  std::size_t pos = 0;
  int depth = 0;

  explicit Parser(const std::string& t) : text(t) {}

  [[noreturn]] void err(const char* what) {
    throw Failure(2, std::string("invalid json: ") + what,
                  "offset " + std::to_string(pos));
  }
  void ws() {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
                                 text[pos] == '\n' || text[pos] == '\r'))
      ++pos;
  }
  bool eat(char c) {
    ws();
    if (pos < text.size() && text[pos] == c) {
      ++pos;
      return true;
    }
    return false;
  }
  Json parse_value() {
    ws();
    if (pos >= text.size()) err("unexpected end");
    if (++depth > 256) err("nesting too deep");
    char c = text[pos];
    Json v;
    if (c == '{') {
      v = Json::object();
      ++pos;
      ws();
      if (eat('}')) {
        --depth;
        return v;
      }
      while (true) {
        ws();
        if (pos >= text.size() || text[pos] != '"') err("expected key");
        std::string key = parse_string();
        if (!eat(':')) err("expected ':'");
        v.set(key, parse_value());
        ws();
        if (eat('}')) break;
        if (!eat(',')) err("expected ',' or '}'");
      }
    } else if (c == '[') {
      v = Json::array();
      ++pos;
      ws();
      if (eat(']')) {
        --depth;
        return v;
      }
      while (true) {
        v.push(parse_value());
        ws();
        if (eat(']')) break;
        if (!eat(',')) err("expected ',' or ']'");
      }
    } else if (c == '"') {
      v = Json(parse_string());
    } else if (c == 't') {
      if (text.compare(pos, 4, "true") != 0) err("bad literal");
      pos += 4;
      v = Json(true);
    } else if (c == 'f') {
      if (text.compare(pos, 5, "false") != 0) err("bad literal");
      pos += 5;
      v = Json(false);
    } else if (c == 'n') {
      if (text.compare(pos, 4, "null") != 0) err("bad literal");
      pos += 4;
      v = Json();
    } else {
      std::size_t start = pos;
      if (text[pos] == '-' || text[pos] == '+') ++pos;
      while (pos < text.size() &&
             ((text[pos] >= '0' && text[pos] <= '9') || text[pos] == '.' ||
              text[pos] == 'e' || text[pos] == 'E' || text[pos] == '-' ||
              text[pos] == '+'))
        ++pos;
      if (pos == start) err("bad number");
      std::string num = text.substr(start, pos - start);
      if (num.find('.') == std::string::npos && num.find('e') == std::string::npos &&
          num.find('E') == std::string::npos) {
        try {
          v = Json(static_cast<i64>(std::stoll(num)));
        } catch (...) {
          v = Json(std::stod(num));
        }
      } else {
        v = Json(std::stod(num));
      }
    }
    --depth;
    return v;
  }
  std::string parse_string() {
    if (text[pos] != '"') err("expected string");
    ++pos;
    std::string out;
    while (pos < text.size()) {
      char c = text[pos++];
      if (c == '"') return out;
      if (c == '\\') {
        if (pos >= text.size()) err("bad escape");
        char e = text[pos++];
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            if (pos + 4 > text.size()) err("bad unicode escape");
            unsigned cp = 0;
            for (int i = 0; i < 4; ++i) {
              char h = text[pos++];
              cp <<= 4;
              if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
              else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
              else err("bad hex");
            }
            if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= text.size() &&
                text[pos] == '\\' && text[pos + 1] == 'u') {
              pos += 2;
              unsigned lo = 0;
              for (int i = 0; i < 4; ++i) {
                char h = text[pos++];
                lo <<= 4;
                if (h >= '0' && h <= '9') lo |= static_cast<unsigned>(h - '0');
                else if (h >= 'a' && h <= 'f') lo |= static_cast<unsigned>(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') lo |= static_cast<unsigned>(h - 'A' + 10);
              }
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            if (cp < 0x80) out.push_back(static_cast<char>(cp));
            else if (cp < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
              out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
              out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
              out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            break;
          }
          default: err("unknown escape");
        }
      } else {
        out.push_back(c);
      }
    }
    err("unterminated string");
  }
};

}  // namespace

Json Json::parse(const std::string& text) {
  Parser p(text);
  Json v = p.parse_value();
  p.ws();
  if (p.pos != text.size()) p.err("trailing data");
  return v;
}

std::optional<Json> Json::try_parse(const std::string& text) {
  try {
    return parse(text);
  } catch (...) {
    return std::nullopt;
  }
}

void Json::dump_to(std::string& out, int indent, int depth) const {
  auto newline = [&](int d) {
    if (indent >= 0) {
      out.push_back('\n');
      out.append(static_cast<std::size_t>(indent * d), ' ');
    }
  };
  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += b_ ? "true" : "false"; break;
    case Type::Int: out += std::to_string(i_); break;
    case Type::Double: number_to_string(d_, out); break;
    case Type::String: escape_json(s_, out); break;
    case Type::Array: {
      out.push_back('[');
      for (std::size_t i = 0; i < arr_.size(); ++i) {
        if (i) out.push_back(',');
        newline(depth + 1);
        arr_[i].dump_to(out, indent, depth + 1);
      }
      if (!arr_.empty()) newline(depth);
      out.push_back(']');
      break;
    }
    case Type::Object: {
      out.push_back('{');
      for (std::size_t i = 0; i < obj_.size(); ++i) {
        if (i) out.push_back(',');
        newline(depth + 1);
        escape_json(obj_[i].first, out);
        out.push_back(':');
        if (indent >= 0) out.push_back(' ');
        obj_[i].second.dump_to(out, indent, depth + 1);
      }
      if (!obj_.empty()) newline(depth);
      out.push_back('}');
      break;
    }
  }
}

std::string Json::dump(int indent) const {
  std::string out;
  dump_to(out, indent, 0);
  return out;
}

Json Json::of(std::initializer_list<std::pair<const char*, Json>> items) {
  Json j = Json::object();
  for (const auto& kv : items) j.set(kv.first, kv.second);
  return j;
}

/* --------------------------------------------------------------- windows */

namespace {
std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}
std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                              nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                      nullptr);
  return s;
}
}  // namespace

namespace fs {

std::string join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  if (b.size() > 1 && (b[1] == ':')) return b;  // absolute drive path wins
  if (!b.empty() && (b[0] == '/' || b[0] == '\\')) {
    // treat as rooted in the same drive
    std::string base = a;
    if (base.size() >= 2 && base[1] == ':') return base.substr(0, 2) + b;
    return b;
  }
  char last = a.back();
  if (last == '/' || last == '\\') return a + b;
  return a + "\\" + b;
}

std::string normalize(const std::string& p) {
  std::string s = p;
  std::replace(s.begin(), s.end(), '\\', '/');
  std::vector<std::string> parts;
  bool rooted = !s.empty() && s[0] == '/';
  std::string prefix;
  std::size_t i = 0;
  if (s.size() >= 2 && s[1] == ':') {
    prefix = s.substr(0, 2);
    i = 2;
    if (i < s.size() && s[i] == '/') rooted = true;
  }
  std::size_t start = i;
  while (start <= s.size()) {
    std::size_t end = s.find('/', start);
    if (end == std::string::npos) end = s.size();
    std::string seg = s.substr(start, end - start);
    if (seg == "..") {
      if (!parts.empty()) parts.pop_back();
    } else if (!seg.empty() && seg != ".") {
      parts.push_back(seg);
    }
    start = end + 1;
  }
  std::string out = prefix;
  if (rooted) out += "/";
  for (std::size_t k = 0; k < parts.size(); ++k) {
    if (k) out += "/";
    out += parts[k];
  }
  if (out.empty()) out = ".";
  return out;
}

std::string parent(const std::string& p) {
  std::string s = normalize(p);
  std::size_t pos = s.find_last_of('/');
  if (pos == std::string::npos) return ".";
  if (pos == 0) return "/";
  if (pos == 2 && s[1] == ':') return s.substr(0, 3);
  return s.substr(0, pos);
}

std::string filename(const std::string& p) {
  std::string s = normalize(p);
  std::size_t pos = s.find_last_of('/');
  return pos == std::string::npos ? s : s.substr(pos + 1);
}

std::string stem(const std::string& p) {
  std::string f = filename(p);
  std::size_t pos = f.find_last_of('.');
  return pos == std::string::npos || pos == 0 ? f : f.substr(0, pos);
}

std::string ext(const std::string& p) {
  std::string f = filename(p);
  std::size_t pos = f.find_last_of('.');
  return pos == std::string::npos || pos == 0 ? std::string() : f.substr(pos);
}

std::string absolute(const std::string& p) {
  std::wstring w = widen(p);
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetFullPathNameW(w.c_str(), static_cast<DWORD>(std::size(buf)), buf, nullptr);
  if (n == 0) return normalize(p);
  return normalize(narrow(std::wstring(buf, n)));
}

bool exists(const std::string& p) {
  DWORD a = GetFileAttributesW(widen(p).c_str());
  return a != INVALID_FILE_ATTRIBUTES;
}
bool is_dir(const std::string& p) {
  DWORD a = GetFileAttributesW(widen(p).c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool is_file(const std::string& p) {
  DWORD a = GetFileAttributesW(widen(p).c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
i64 size(const std::string& p) {
  WIN32_FILE_ATTRIBUTE_DATA d{};
  if (!GetFileAttributesExW(widen(p).c_str(), GetFileExInfoStandard, &d)) return -1;
  return (static_cast<i64>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}
i64 mtime_ms(const std::string& p) {
  WIN32_FILE_ATTRIBUTE_DATA d{};
  if (!GetFileAttributesExW(widen(p).c_str(), GetFileExInfoStandard, &d)) return 0;
  ULARGE_INTEGER u;
  u.LowPart = d.ftLastWriteTime.dwLowDateTime;
  u.HighPart = d.ftLastWriteTime.dwHighDateTime;
  return static_cast<i64>(u.QuadPart / 10000ULL) - 11644473600000LL;
}
void mkdirs(const std::string& p) {
  std::string s = normalize(p);
  std::string cur;
  std::size_t i = 0;
  if (s.size() >= 2 && s[1] == ':') {
    cur = s.substr(0, 2);
    i = 2;
  }
  while (i <= s.size()) {
    std::size_t next = s.find('/', i);
    if (next == std::string::npos) next = s.size();
    if (next > i) {
      cur += "/";
      cur += s.substr(i, next - i);
      CreateDirectoryW(widen(cur).c_str(), nullptr);
    }
    i = next + 1;
  }
}
std::vector<std::string> list_dir(const std::string& p) {
  std::vector<std::string> out;
  std::string pattern = join(p, "*");
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW(widen(pattern).c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    std::string name = narrow(fd.cFileName);
    if (name == "." || name == "..") continue;
    out.push_back(name);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(out.begin(), out.end());
  return out;
}
void remove_file(const std::string& p) { DeleteFileW(widen(p).c_str()); }
void remove_all(const std::string& p) {
  if (!exists(p)) return;
  if (is_dir(p)) {
    for (const auto& name : list_dir(p)) remove_all(join(p, name));
    RemoveDirectoryW(widen(p).c_str());
  } else {
    SetFileAttributesW(widen(p).c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(widen(p).c_str());
  }
}
void rename(const std::string& from, const std::string& to) {
  if (!MoveFileExW(widen(from).c_str(), widen(to).c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
    throw Failure(3, "cannot rename file", from + " -> " + to);
  }
}
std::string read_file(const std::string& p) {
  std::string out;
  if (!read_file_opt(p, out)) throw Failure(4, "cannot read file", p);
  return out;
}
bool read_file_opt(const std::string& p, std::string& out) {
  HANDLE h = CreateFileW(widen(p).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz)) {
    CloseHandle(h);
    return false;
  }
  out.resize(static_cast<std::size_t>(sz.QuadPart));
  std::size_t off = 0;
  while (off < out.size()) {
    DWORD got = 0;
    DWORD want = static_cast<DWORD>(std::min<std::size_t>(out.size() - off, 1u << 24));
    if (!ReadFile(h, out.data() + off, want, &got, nullptr) || got == 0) break;
    off += got;
  }
  CloseHandle(h);
  out.resize(off);
  return off == static_cast<std::size_t>(sz.QuadPart);
}
void write_file(const std::string& p, const std::string& data) {
  mkdirs(parent(p));
  HANDLE h = CreateFileW(widen(p).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) throw Failure(5, "cannot write file", p);
  std::size_t off = 0;
  while (off < data.size()) {
    DWORD wrote = 0;
    DWORD want = static_cast<DWORD>(std::min<std::size_t>(data.size() - off, 1u << 24));
    if (!WriteFile(h, data.data() + off, want, &wrote, nullptr)) {
      CloseHandle(h);
      throw Failure(5, "cannot write file", p);
    }
    off += wrote;
  }
  CloseHandle(h);
}
void write_file_atomic(const std::string& p, const std::string& data) {
  std::string tmp = p + ".tmp" + std::to_string(GetCurrentProcessId());
  write_file(tmp, data);
  if (!MoveFileExW(widen(tmp).c_str(), widen(p).c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    remove_file(tmp);
    throw Failure(6, "cannot replace file", p);
  }
}
void copy_file(const std::string& from, const std::string& to) {
  mkdirs(parent(to));
  if (!CopyFileW(widen(from).c_str(), widen(to).c_str(), FALSE))
    throw Failure(7, "cannot copy file", from + " -> " + to);
}
std::string temp_dir() {
  wchar_t buf[MAX_PATH + 1];
  DWORD n = GetTempPathW(MAX_PATH, buf);
  std::string out = n ? narrow(std::wstring(buf, n)) : "C:\\Windows\\Temp";
  while (!out.empty() && (out.back() == '\\' || out.back() == '/')) out.pop_back();
  return out;
}
std::string appdata_dir() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH * 2);
  if (!n) return temp_dir();
  return narrow(std::wstring(buf, n));
}
u64 free_space(const std::string& p) {
  ULARGE_INTEGER avail{};
  if (!GetDiskFreeSpaceExW(widen(p).c_str(), &avail, nullptr, nullptr)) return 0;
  return avail.QuadPart;
}

}  // namespace fs

i64 now_ms() {
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return static_cast<i64>(u.QuadPart / 10000ULL) - 11644473600000LL;
}

std::string iso_time(i64 ms) {
  if (ms <= 0) return "-";
  std::time_t secs = static_cast<std::time_t>(ms / 1000);
  std::tm tmv{};
  gmtime_s(&tmv, &secs);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tmv.tm_year + 1900,
                tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return buf;
}

std::string human_duration(i64 ms) {
  if (ms < 0) ms = 0;
  i64 total_s = ms / 1000;
  i64 h = total_s / 3600, m = (total_s % 3600) / 60, s = total_s % 60;
  char buf[64];
  if (h > 0) std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", h, m, s);
  else std::snprintf(buf, sizeof(buf), "%02lld:%02lld", m, s);
  return buf;
}

std::string human_size(u64 bytes) {
  static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double v = static_cast<double>(bytes);
  int i = 0;
  while (v >= 1024.0 && i < 4) {
    v /= 1024.0;
    ++i;
  }
  char buf[64];
  if (i == 0) std::snprintf(buf, sizeof(buf), "%llu%s", static_cast<unsigned long long>(bytes),
                            units[i]);
  else std::snprintf(buf, sizeof(buf), "%.2f%s", v, units[i]);
  return buf;
}

std::string hex_encode(const u8* data, std::size_t n) {
  static const char* digits = "0123456789abcdef";
  std::string out(n * 2, '0');
  for (std::size_t i = 0; i < n; ++i) {
    out[i * 2] = digits[data[i] >> 4];
    out[i * 2 + 1] = digits[data[i] & 0xF];
  }
  return out;
}
std::string hex_encode(const std::string& raw) {
  return hex_encode(reinterpret_cast<const u8*>(raw.data()), raw.size());
}
bool hex_decode(const std::string& hex, std::string& out) {
  if (hex.size() % 2) return false;
  out.clear();
  out.reserve(hex.size() / 2);
  auto val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    int hi = val(hex[i]), lo = val(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out.push_back(static_cast<char>((hi << 4) | lo));
  }
  return true;
}

std::string random_hex(std::size_t bytes) {
  static std::mt19937_64 rng(std::random_device{}());
  std::string out;
  out.reserve(bytes * 2);
  for (std::size_t i = 0; i < bytes; i += 8) {
    u64 v = rng();
    for (int k = 0; k < 8 && out.size() < bytes * 2; ++k) {
      static const char* digits = "0123456789abcdef";
      out.push_back(digits[(v >> (k * 8 + 4)) & 0xF]);
      out.push_back(digits[(v >> (k * 8)) & 0xF]);
    }
  }
  return out;
}

std::string to_lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string to_upper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::string trim(const std::string& s) {
  std::size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  std::size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}
std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    std::size_t pos = s.find(sep, start);
    if (pos == std::string::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}
bool starts_with(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
bool ends_with(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
std::string replace_all(std::string s, const std::string& from, const std::string& to) {
  if (from.empty()) return s;
  std::size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

}  // namespace vd
