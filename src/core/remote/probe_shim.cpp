/* Probe-only shim: minimal implementations of the core.h / ui.h symbols that the
   remote layer needs. It exists so src/core/remote/probe.cpp can link and run
   before the real core module lands; it is NOT part of the main build and must
   never be compiled into vanadium.dll. */
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/remote.h"

namespace fsys = std::filesystem;

namespace {

fsys::path to_path(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return fsys::path(w);
}

std::string from_path(const fsys::path& p) {
  std::wstring w = p.wstring();
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                              nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                      nullptr);
  return s;
}

void emit_event(const vd_event_cb cb, void* user, int kind, const char* a, const char* b,
                long long i1 = 0, long long i2 = 0, long long i3 = 0, long long i4 = 0) {
  if (!cb) return;
  vd_event ev{};
  ev.kind = kind;
  ev.a = a;
  ev.b = b;
  ev.i1 = i1;
  ev.i2 = i2;
  ev.i3 = i3;
  ev.i4 = i4;
  cb(user, &ev);
}

}  // namespace

namespace vd {

void fail(int code, const std::string& message, const std::string& detail) {
  throw Failure(code, message, detail);
}

void Emitter::line(const std::string& text) const {
  emit_event(cb_, user_, VD_EV_LINE, text.c_str(), nullptr);
}
void Emitter::warn(const std::string& text) const {
  emit_event(cb_, user_, VD_EV_WARN, text.c_str(), nullptr);
}
void Emitter::status(const std::string& key, const std::string& text) const {
  emit_event(cb_, user_, VD_EV_STATUS, key.c_str(), text.c_str());
}
void Emitter::clear_status() const { emit_event(cb_, user_, VD_EV_STATUS_CLEAR, nullptr, nullptr); }
void Emitter::progress(const std::string& phase, long long done, long long total,
                       long long bytes, long long flags) const {
  emit_event(cb_, user_, VD_EV_PROGRESS, phase.c_str(), nullptr, done, total, bytes, flags);
}
bool Emitter::prompt(const std::string& question, const std::string& hint) const {
  return emit_event(cb_, user_, VD_EV_PROMPT, question.c_str(), hint.c_str()), false;
}
void Emitter::summary(const std::string& json) const {
  emit_event(cb_, user_, VD_EV_SUMMARY, json.c_str(), nullptr);
}
void Emitter::title(const std::string& text) const {
  emit_event(cb_, user_, VD_EV_TITLE, text.c_str(), nullptr);
}
void Emitter::error(int code, const std::string& text) const {
  emit_event(cb_, user_, VD_EV_ERROR, text.c_str(), nullptr, code);
}

/* ------------------------------- Json ---------------------------------- */
Json::Json() = default;
Json::Json(std::nullptr_t) {}
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
Json Json::parse(const std::string&) { fail(2, "Json::parse is not available in the probe shim"); }
std::optional<Json> Json::try_parse(const std::string&) { return std::nullopt; }

bool Json::as_bool(bool def) const { return type_ == Type::Bool ? b_ : def; }
i64 Json::as_int(i64 def) const {
  if (type_ == Type::Int) return i_;
  if (type_ == Type::Double) return static_cast<i64>(d_);
  return def;
}
double Json::as_double(double def) const {
  if (type_ == Type::Double) return d_;
  if (type_ == Type::Int) return static_cast<double>(i_);
  return def;
}
const std::string& Json::as_string() const {
  static const std::string empty;
  return type_ == Type::String ? s_ : empty;
}
std::string Json::as_string_or(const std::string& def) const {
  return type_ == Type::String ? s_ : def;
}

Json& Json::operator[](const std::string& key) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    obj_.clear();
  }
  for (auto& kv : obj_) {
    if (kv.first == key) return kv.second;
  }
  obj_.emplace_back(key, Json());
  return obj_.back().second;
}
const Json& Json::operator[](const std::string& key) const {
  static const Json null_json;
  if (type_ != Type::Object) return null_json;
  for (const auto& kv : obj_) {
    if (kv.first == key) return kv.second;
  }
  return null_json;
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
  static const Json null_json;
  if (type_ != Type::Array || idx >= arr_.size()) return null_json;
  return arr_[idx];
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
  if (type_ != Type::Object) return false;
  for (const auto& kv : obj_) {
    if (kv.first == key) return true;
  }
  return false;
}
std::size_t Json::size() const {
  if (type_ == Type::Array) return arr_.size();
  if (type_ == Type::Object) return obj_.size();
  return 0;
}
std::vector<std::string> Json::keys() const {
  std::vector<std::string> out;
  if (type_ == Type::Object) {
    for (const auto& kv : obj_) out.push_back(kv.first);
  }
  return out;
}
void Json::erase(const std::string& key) {
  if (type_ != Type::Object) return;
  for (size_t i = 0; i < obj_.size(); ++i) {
    if (obj_[i].first == key) {
      obj_.erase(obj_.begin() + static_cast<long>(i));
      return;
    }
  }
}

std::string Json::dump(int indent) const {
  std::string out;
  dump_to(out, indent, 0);
  return out;
}

void Json::dump_to(std::string& out, int indent, int depth) const {
  auto pad = [&](int level) {
    if (indent < 0) return;
    out += "\n";
    out.append(static_cast<size_t>(indent * level), ' ');
  };
  switch (type_) {
    case Type::Null:
      out += "null";
      break;
    case Type::Bool:
      out += b_ ? "true" : "false";
      break;
    case Type::Int:
      out += std::to_string(i_);
      break;
    case Type::Double: {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.17g", d_);
      out += buf;
      break;
    }
    case Type::String: {
      out += '"';
      for (char c : s_) {
        switch (c) {
          case '"': out += "\\\""; break;
          case '\\': out += "\\\\"; break;
          case '\n': out += "\\n"; break;
          case '\r': out += "\\r"; break;
          case '\t': out += "\\t"; break;
          default:
            if (static_cast<unsigned char>(c) < 0x20) {
              char esc[8];
              std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(c));
              out += esc;
            } else {
              out += c;
            }
        }
      }
      out += '"';
      break;
    }
    case Type::Array: {
      out += '[';
      for (size_t i = 0; i < arr_.size(); ++i) {
        if (i) out += indent >= 0 ? "," : ",";
        if (indent >= 0) pad(depth + 1);
        arr_[i].dump_to(out, indent, depth + 1);
      }
      if (indent >= 0 && !arr_.empty()) pad(depth);
      out += ']';
      break;
    }
    case Type::Object: {
      out += '{';
      for (size_t i = 0; i < obj_.size(); ++i) {
        if (i) out += ",";
        if (indent >= 0) pad(depth + 1);
        Json key(obj_[i].first);
        key.dump_to(out, -1, 0);
        out += indent >= 0 ? ": " : ":";
        obj_[i].second.dump_to(out, indent, depth + 1);
      }
      if (indent >= 0 && !obj_.empty()) pad(depth);
      out += '}';
      break;
    }
  }
}

Json Json::of(std::initializer_list<std::pair<const char*, Json>> items) {
  Json j = object();
  for (const auto& kv : items) j.set(kv.first, kv.second);
  return j;
}

/* ------------------------------ filesystem ------------------------------ */
namespace fs {

std::string normalize(const std::string& p) {
  std::string s = p;
  for (char& c : s) {
    if (c == '\\') c = '/';
  }
  return s;
}
std::string join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return normalize(a) + "/" + normalize(b);
}
std::string parent(const std::string& p) {
  fsys::path q = to_path(normalize(p));
  return from_path(q.parent_path());
}
std::string filename(const std::string& p) {
  return from_path(to_path(normalize(p)).filename());
}
std::string stem(const std::string& p) { return from_path(to_path(normalize(p)).stem()); }
std::string ext(const std::string& p) { return from_path(to_path(normalize(p)).extension()); }
std::string absolute(const std::string& p) {
  std::error_code ec;
  auto q = fsys::absolute(to_path(p), ec);
  return ec ? normalize(p) : from_path(q);
}
bool exists(const std::string& p) {
  std::error_code ec;
  return fsys::exists(to_path(p), ec);
}
bool is_dir(const std::string& p) {
  std::error_code ec;
  return fsys::is_directory(to_path(p), ec);
}
bool is_file(const std::string& p) {
  std::error_code ec;
  return fsys::is_regular_file(to_path(p), ec);
}
i64 size(const std::string& p) {
  std::error_code ec;
  auto n = fsys::file_size(to_path(p), ec);
  return ec ? -1 : static_cast<i64>(n);
}
i64 mtime_ms(const std::string& p) {
  std::error_code ec;
  auto t = fsys::last_write_time(to_path(p), ec);
  if (ec) return 0;
  return std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
}
void mkdirs(const std::string& p) {
  if (p.empty()) return;
  std::error_code ec;
  fsys::create_directories(to_path(p), ec);
}
void remove_all(const std::string& p) {
  std::error_code ec;
  fsys::remove_all(to_path(p), ec);
}
void remove_file(const std::string& p) {
  std::error_code ec;
  fsys::remove(to_path(p), ec);
}
void rename(const std::string& from, const std::string& to) {
  std::error_code ec;
  fsys::rename(to_path(from), to_path(to), ec);
  if (ec) fail(3, "rename failed", from + " -> " + to);
}
std::vector<std::string> list_dir(const std::string& p) {
  std::vector<std::string> out;
  std::error_code ec;
  for (const auto& entry : fsys::directory_iterator(to_path(p), ec)) {
    out.push_back(from_path(entry.path().filename()));
  }
  std::sort(out.begin(), out.end());
  return out;
}
std::string read_file(const std::string& p) {
  std::ifstream in(to_path(p), std::ios::binary);
  if (!in) fail(4, "cannot read file", p);
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return data;
}
bool read_file_opt(const std::string& p, std::string& out) {
  std::ifstream in(to_path(p), std::ios::binary);
  if (!in) return false;
  out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return true;
}
void write_file(const std::string& p, const std::string& data) {
  std::ofstream out(to_path(p), std::ios::binary | std::ios::trunc);
  if (!out) fail(5, "cannot write file", p);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
}
void write_file_atomic(const std::string& p, const std::string& data) {
  std::string tmp = p + ".tmp";
  write_file(tmp, data);
  if (exists(p)) remove_file(p);
  rename(tmp, p);
}
void copy_file(const std::string& from, const std::string& to) {
  std::error_code ec;
  fsys::copy_file(to_path(from), to_path(to), fsys::copy_options::overwrite_existing, ec);
  if (ec) fail(6, "copy failed", from + " -> " + to);
}
std::string temp_dir() {
  wchar_t buf[MAX_PATH + 1] = {0};
  DWORD n = GetTempPathW(MAX_PATH, buf);
  return n ? from_path(fsys::path(std::wstring(buf, n))) : std::string(".");
}
std::string appdata_dir() {
  wchar_t* value = nullptr;
  size_t len = 0;
  if (_wdupenv_s(&value, &len, L"APPDATA") == 0 && value) {
    std::string out = from_path(fsys::path(std::wstring(value)));
    free(value);
    return out;
  }
  return temp_dir();
}
u64 free_space(const std::string& p) {
  std::error_code ec;
  auto info = fsys::space(to_path(p), ec);
  return ec ? 0 : static_cast<u64>(info.available);
}

}  // namespace fs

/* ------------------------------ time / misc ----------------------------- */
i64 now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
std::string iso_time(i64 ms) {
  std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  gmtime_s(&tm, &t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}
std::string human_duration(i64 ms) {
  if (ms < 1000) return std::to_string(ms) + "ms";
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1fs", static_cast<double>(ms) / 1000.0);
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
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f%s", v, units[i]);
  return buf;
}
std::string hex_encode(const std::string& raw) {
  static const char* hex = "0123456789abcdef";
  std::string out;
  for (unsigned char c : raw) {
    out += hex[c >> 4];
    out += hex[c & 0x0f];
  }
  return out;
}
std::string hex_encode(const u8* data, std::size_t n) {
  return hex_encode(std::string(reinterpret_cast<const char*>(data), n));
}
bool hex_decode(const std::string& hex, std::string& out) {
  if (hex.size() % 2) return false;
  auto val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  out.clear();
  for (size_t i = 0; i < hex.size(); i += 2) {
    int hi = val(hex[i]), lo = val(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out += static_cast<char>((hi << 4) | lo);
  }
  return true;
}
std::string random_hex(std::size_t bytes) {
  std::random_device rd;
  std::string out;
  for (std::size_t i = 0; i < bytes; ++i) {
    unsigned v = rd() & 0xff;
    static const char* hex = "0123456789abcdef";
    out += hex[v >> 4];
    out += hex[v & 0x0f];
  }
  return out;
}
std::string to_lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}
std::string to_upper(std::string s) {
  for (char& c : s) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return s;
}
std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
  return s.substr(a, b - a);
}
std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  out.push_back(cur);
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
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

}  // namespace vd
