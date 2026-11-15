#pragma once
#include <string>

namespace vd {

enum Lang { LANG_EN = 0, LANG_ZH_CN = 1, LANG_TR_CN = 2, LANG_AM_EN = 3, LANG_COUNT = 4 };

Lang lang_from_code(const std::string& code);
const char* lang_code(Lang lang);
const char* lang_name(Lang lang);

/* Message lookup. Keys come from schema/messages.json; generated catalogs live in
   src/gen/i18n_catalog.cpp. Missing key returns the key itself (visible, not silent). */
const char* msg(const std::string& key);
const char* msg_lang(const std::string& key, Lang lang);
void set_language(Lang lang);
Lang language();

/* printf-style formatting on top of msg(). */
std::string msgf(const std::string& key, const std::string& a1);
std::string msgf2(const std::string& key, const std::string& a1, const std::string& a2);

}  // namespace vd
