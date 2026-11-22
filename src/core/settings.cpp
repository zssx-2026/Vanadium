#include "vanadium/settings.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/i18n.h"

namespace vd {
namespace {

const std::vector<SettingDef>& make_defs() {
  static const std::vector<SettingDef> defs = {
      /* key, type, default, min_i, max_i, min_f, max_f, dynamic, help_key */
      {"max_branch", SettingType::Int, "1024", 1, 1000000, 0.0, 0.0, false, "help.set.max_branch"},
      {"max_child_branch", SettingType::Int, "256", 1, 1000000, 0.0, 0.0, false, "help.set.max_child_branch"},
      {"local_temp_max_size", SettingType::Float, "8.0", 0, 0, 0.1, 4096.0, false, "help.set.local_temp_max_size"},
      {"local_temp", SettingType::Bool, "true", 0, 0, 0.0, 0.0, false, "help.set.local_temp"},
      {"lang", SettingType::String, "en", 0, 0, 0.0, 0.0, false, "help.set.lang"},
      {"defult_remote_basic_url", SettingType::String, "https://github.com", 0, 0, 0.0, 0.0, false,
       "help.set.defult_remote_basic_url"},
      {"url_", SettingType::String, "", 0, 0, 0.0, 0.0, true, "help.set.url"},
  };
  return defs;
}

bool is_lang_code(const std::string& v) {
  const std::string s = to_lower(trim(v));
  return s == "en" || s == "zh-cn" || s == "tr-cn" || s == "am-en";
}

bool parse_bool(const std::string& raw, bool& out) {
  const std::string s = to_lower(trim(raw));
  if (s == "true" || s == "1" || s == "yes" || s == "on" || s == "y") {
    out = true;
    return true;
  }
  if (s == "false" || s == "0" || s == "no" || s == "off" || s == "n") {
    out = false;
    return true;
  }
  return false;
}

Failure invalid(const std::string& key, const std::string& value, const std::string& why) {
  Failure f;
  f.code = 2001;
  f.message = msgf2("set.invalid", key, value);
  f.detail = why;
  return f;
}

bool looks_like_url(const std::string& v) {
  return starts_with(v, "http://") || starts_with(v, "https://") || starts_with(v, "ssh://") ||
         starts_with(v, "git@") || starts_with(v, "file://");
}

}  // namespace

const std::vector<SettingDef>& Settings::defs() { return make_defs(); }

const SettingDef* Settings::find_def(const std::string& key) {
  for (const auto& d : defs()) {
    if (d.dynamic) {
      if (starts_with(key, d.key) && key.size() > d.key.size()) return &d;
      continue;
    }
    if (d.key == key) return &d;
  }
  return nullptr;
}

void Settings::load(const std::string& path) {
  values_.clear();
  std::string text;
  if (!fs::read_file_opt(path, text) || trim(text).empty()) {
    for (const auto& d : defs()) {
      if (!d.dynamic) values_[d.key] = d.default_value;
    }
    return;
  }
  Json doc;
  auto parsed = Json::try_parse(text);
  if (!parsed) return;
  doc = *parsed;
  const Json* obj = doc.is_object() && doc.contains("settings") ? &doc["settings"] : &doc;
  if (obj->is_object()) {
    for (const auto& key : obj->keys()) {
      const Json& v = (*obj)[key];
      std::string raw;
      if (v.is_string()) raw = v.as_string();
      else if (v.is_bool()) raw = v.as_bool() ? "true" : "false";
      else if (v.is_number()) raw = v.dump();
      else continue;
      if (find_def(key)) values_[key] = raw;
    }
  }
  for (const auto& d : defs()) {
    if (!d.dynamic && values_.find(d.key) == values_.end()) values_[d.key] = d.default_value;
  }
}

void Settings::save(const std::string& path) const {
  Json obj = Json::object();
  for (const auto& kv : values_) {
    const SettingDef* d = find_def(kv.first);
    if (d && d->type == SettingType::Int) {
      obj.set(kv.first, Json(static_cast<i64>(std::atoll(kv.second.c_str()))));
    } else if (d && d->type == SettingType::Float) {
      obj.set(kv.first, Json(std::atof(kv.second.c_str())));
    } else if (d && d->type == SettingType::Bool) {
      bool b = false;
      parse_bool(kv.second, b);
      obj.set(kv.first, Json(b));
    } else {
      obj.set(kv.first, Json(kv.second));
    }
  }
  Json doc = Json::object();
  doc.set("version", Json(1));
  doc.set("settings", obj);
  fs::write_file_atomic(path, doc.dump(2) + "\n");
}

void Settings::unset(const std::string& key) { values_.erase(key); }

std::string Settings::get(const std::string& key) const {
  auto it = values_.find(key);
  if (it != values_.end()) return it->second;
  const SettingDef* d = find_def(key);
  return d ? d->default_value : std::string();
}

i64 Settings::get_int(const std::string& key) const {
  return static_cast<i64>(std::atoll(get(key).c_str()));
}

double Settings::get_float(const std::string& key) const {
  return std::atof(get(key).c_str());
}

bool Settings::get_bool(const std::string& key) const {
  bool b = false;
  parse_bool(get(key), b);
  return b;
}

void Settings::set(const std::string& key, const std::string& value) {
  const SettingDef* d = find_def(key);
  if (!d) {
    Failure f;
    f.code = 2002;
    f.message = msgf("set.unknown_key", key);
    throw f;
  }
  if (d->dynamic) {
    const std::string name = key.substr(d->key.size());
    if (name.empty() || name.find_first_of(" \t\r\n/\\") != std::string::npos) {
      throw invalid(key, value, "dynamic setting name must be a simple identifier");
    }
  }
  switch (d->type) {
    case SettingType::Int: {
      char* end = nullptr;
      const long long v = std::strtoll(value.c_str(), &end, 10);
      if (!end || *end != '\0' || end == value.c_str()) throw invalid(key, value, "not an integer");
      if (d->max_i > d->min_i && (v < d->min_i || v > d->max_i)) {
        throw invalid(key, value, "out of range");
      }
      values_[key] = std::to_string(v);
      break;
    }
    case SettingType::Float: {
      char* end = nullptr;
      const double v = std::strtod(value.c_str(), &end);
      if (!end || *end != '\0' || end == value.c_str() || !std::isfinite(v)) {
        throw invalid(key, value, "not a number");
      }
      if (d->max_f > d->min_f && (v < d->min_f || v > d->max_f)) {
        throw invalid(key, value, "out of range");
      }
      values_[key] = std::to_string(v);
      break;
    }
    case SettingType::Bool: {
      bool b = false;
      if (!parse_bool(value, b)) throw invalid(key, value, "expected true/false");
      values_[key] = b ? "true" : "false";
      break;
    }
    case SettingType::String: {
      std::string v = value;
      if (key == "lang") {
        v = to_lower(trim(v));
        if (!is_lang_code(v)) throw invalid(key, value, "lang must be en, zh-cn, tr-cn or am-en");
        set_language(lang_from_code(v));
      } else if (key == "defult_remote_basic_url") {
        v = trim(v);
        while (!v.empty() && v.back() == '/') v.pop_back();
        if (!looks_like_url(v)) throw invalid(key, value, "expected http(s) url");
      } else if (d->dynamic) {
        v = trim(v);
        if (!looks_like_url(v)) throw invalid(key, value, "expected http(s) url");
      }
      values_[key] = v;
      break;
    }
  }
}

std::vector<std::string> Settings::keys() const {
  std::vector<std::string> out;
  out.reserve(values_.size());
  for (const auto& kv : values_) out.push_back(kv.first);
  return out;
}

}  // namespace vd
