#include "vanadium/i18n.h"

#include <cctype>
#include <string>

#include "vanadium/core.h"

/* The generated catalog (src/gen/i18n_catalog.cpp) owns the message tables.
   Contract: returns nullptr when the key or language is unknown. */
namespace vd::gen {
const char* msg_lookup(const char* key, int lang);
int msg_catalog_count();
}  // namespace vd::gen

namespace vd {
namespace {

Lang g_lang = LANG_EN;

std::string normalize(const std::string& code) {
  std::string s;
  s.reserve(code.size());
  for (char c : code) {
    if (c == '_' || c == ' ') {
      s.push_back('-');
    } else {
      s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  return s;
}

}  // namespace

Lang lang_from_code(const std::string& code) {
  const std::string s = normalize(code);
  if (s.empty() || s == "en" || s == "en-us" || s == "am-en" || s == "en-gb") {
    return s == "am-en" ? LANG_AM_EN : LANG_EN;
  }
  if (s == "zh" || s == "zh-cn" || s == "zh-hans" || s == "cn" || s == "zh-sg") return LANG_ZH_CN;
  if (s == "tr" || s == "tr-cn" || s == "zh-tw" || s == "zh-hant" || s == "zh-hk" || s == "tw")
    return LANG_TR_CN;
  if (s == "am" || s == "am-en" || s == "en-am") return LANG_AM_EN;
  return LANG_EN;
}

const char* lang_code(Lang lang) {
  switch (lang) {
    case LANG_ZH_CN: return "zh-cn";
    case LANG_TR_CN: return "tr-cn";
    case LANG_AM_EN: return "am-en";
    case LANG_EN:
    default: return "en";
  }
}

const char* lang_name(Lang lang) {
  switch (lang) {
    case LANG_ZH_CN: return "简体中文";
    case LANG_TR_CN: return "繁體中文";
    case LANG_AM_EN: return "American English";
    case LANG_EN:
    default: return "English";
  }
}

const char* msg_lang(const std::string& key, Lang lang) {
  const char* v = gen::msg_lookup(key.c_str(), static_cast<int>(lang));
  if (v) return v;
  if (lang != LANG_EN) {
    v = gen::msg_lookup(key.c_str(), static_cast<int>(LANG_EN));
    if (v) return v;
  }
  /* Visible fallback: the key itself, so a missing message is never silent. */
  return key.c_str();
}

const char* msg(const std::string& key) { return msg_lang(key, g_lang); }

void set_language(Lang lang) {
  if (lang >= 0 && lang < LANG_COUNT) g_lang = lang;
}

Lang language() { return g_lang; }

namespace {

std::string substitute(const std::string& text, const std::string& a1, const std::string& a2,
                       bool has_a2) {
  std::string out;
  out.reserve(text.size() + a1.size() + a2.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '%' && i + 1 < text.size()) {
      const char n = text[i + 1];
      if (n == '1') {
        out += a1;
        ++i;
        continue;
      }
      if (n == '2' && has_a2) {
        out += a2;
        ++i;
        continue;
      }
      if (n == 's') {
        out += has_a2 ? a2 : a1;
        ++i;
        continue;
      }
      if (n == '%') {
        out.push_back('%');
        ++i;
        continue;
      }
    }
    if (c == '{' && i + 2 < text.size() && text[i + 2] == '}') {
      if (text[i + 1] == '1') {
        out += a1;
        i += 2;
        continue;
      }
      if (text[i + 1] == '2' && has_a2) {
        out += a2;
        i += 2;
        continue;
      }
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace

std::string msgf(const std::string& key, const std::string& a1) {
  return substitute(msg(key), a1, std::string(), false);
}

std::string msgf2(const std::string& key, const std::string& a1, const std::string& a2) {
  return substitute(msg(key), a1, a2, true);
}

}  // namespace vd
