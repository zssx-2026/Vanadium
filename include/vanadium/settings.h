#pragma once
#include <map>
#include <string>
#include <vector>
#include "core.h"

namespace vd {

enum class SettingType { Int, Float, Bool, String };

struct SettingDef {
  std::string key;
  SettingType type;
  std::string default_value;
  long long min_i = 0;
  long long max_i = 0;
  double min_f = 0.0;
  double max_f = 0.0;
  bool dynamic = false;      /* url_<name> style */
  const char* help_key = nullptr;
};

class Settings {
 public:
  static const std::vector<SettingDef>& defs();
  static const SettingDef* find_def(const std::string& key);

  void load(const std::string& path);
  void save(const std::string& path) const;

  std::string get(const std::string& key) const;
  i64 get_int(const std::string& key) const;
  double get_float(const std::string& key) const;
  bool get_bool(const std::string& key) const;

  /* Validates and applies; throws Failure with a localized reason. */
  void set(const std::string& key, const std::string& value);

  /* Drops an explicit value so the default applies again. */
  void unset(const std::string& key);

  std::vector<std::string> keys() const;
  const std::map<std::string, std::string>& raw() const { return values_; }

 private:
  std::map<std::string, std::string> values_;
};

}  // namespace vd
