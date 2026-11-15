#pragma once
/* Common kernel types: failure, json, fs, time, hex. */
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vd {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i64 = std::int64_t;

/* Thrown by kernel code; translated to VD_EV_ERROR at the ABI boundary. */
struct Failure {
  int code = 1;
  std::string message;
  std::string detail;
  Failure() = default;
  Failure(int c, std::string m, std::string d = {})
      : code(c), message(std::move(m)), detail(std::move(d)) {}
};

[[noreturn]] void fail(int code, const std::string& message, const std::string& detail = {});

class Json {
 public:
  enum class Type { Null, Bool, Int, Double, String, Array, Object };

  Json();
  Json(std::nullptr_t);
  Json(bool v);
  Json(int v);
  Json(i64 v);
  Json(double v);
  Json(const char* v);
  Json(std::string v);
  Json(const Json&) = default;
  Json(Json&&) = default;
  Json& operator=(const Json&) = default;
  Json& operator=(Json&&) = default;

  static Json array();
  static Json object();
  static Json parse(const std::string& text);
  static std::optional<Json> try_parse(const std::string& text);

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_int() const { return type_ == Type::Int; }
  bool is_number() const { return type_ == Type::Int || type_ == Type::Double; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  bool as_bool(bool def = false) const;
  i64 as_int(i64 def = 0) const;
  double as_double(double def = 0) const;
  const std::string& as_string() const;
  std::string as_string_or(const std::string& def) const;

  Json& operator[](const std::string& key);
  const Json& operator[](const std::string& key) const;
  Json& operator[](std::size_t idx);
  const Json& operator[](std::size_t idx) const;

  void push(Json v);
  void set(const std::string& key, Json v);
  bool contains(const std::string& key) const;
  std::size_t size() const;
  std::vector<std::string> keys() const;
  void erase(const std::string& key);

  std::string dump(int indent = -1) const;
  void dump_to(std::string& out, int indent, int depth) const;

  /* Convenience builders */
  static Json of(std::initializer_list<std::pair<const char*, Json>> items);

 private:
  Type type_ = Type::Null;
  bool b_ = false;
  i64 i_ = 0;
  double d_ = 0.0;
  std::string s_;
  std::vector<Json> arr_;
  std::vector<std::pair<std::string, Json>> obj_;
};

/* ---------- filesystem ---------- */
namespace fs {
std::string join(const std::string& a, const std::string& b);
std::string normalize(const std::string& p);           /* collapse . and .., use / */
std::string parent(const std::string& p);
std::string filename(const std::string& p);
std::string stem(const std::string& p);
std::string ext(const std::string& p);
std::string absolute(const std::string& p);
bool exists(const std::string& p);
bool is_dir(const std::string& p);
bool is_file(const std::string& p);
i64 size(const std::string& p);
i64 mtime_ms(const std::string& p);
void mkdirs(const std::string& p);
void remove_all(const std::string& p);
void remove_file(const std::string& p);
void rename(const std::string& from, const std::string& to);
std::vector<std::string> list_dir(const std::string& p);      /* names, sorted */
std::string read_file(const std::string& p);
bool read_file_opt(const std::string& p, std::string& out);
void write_file(const std::string& p, const std::string& data);
void write_file_atomic(const std::string& p, const std::string& data); /* .tmp + rename */
void copy_file(const std::string& from, const std::string& to);
std::string temp_dir();
std::string appdata_dir();
u64 free_space(const std::string& p);
}  // namespace fs

/* ---------- time / misc ---------- */
i64 now_ms();
std::string iso_time(i64 ms);
std::string human_duration(i64 ms);
std::string human_size(u64 bytes);
std::string hex_encode(const std::string& raw);
std::string hex_encode(const u8* data, std::size_t n);
bool hex_decode(const std::string& hex, std::string& out);
std::string random_hex(std::size_t bytes);
std::string to_lower(std::string s);
std::string to_upper(std::string s);
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char sep);
bool starts_with(const std::string& s, const std::string& p);
bool ends_with(const std::string& s, const std::string& p);
std::string replace_all(std::string s, const std::string& from, const std::string& to);

}  // namespace vd
