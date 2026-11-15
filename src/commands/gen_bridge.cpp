/* Bridge between the kernel and the generated tables.
   The generator emits the implementations in src/gen; this unit exposes the few of
   them that need a C++-friendly surface (UTF-8 string widths and std::string globs)
   so the command layer can use them without touching generated code. */
#include "vanadium/gen.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace vd::gen {
namespace {

/* Decode one UTF-8 scalar value; advances pos. Malformed input yields U+FFFD and
   consumes a single byte, so a bad sequence never stalls the caller. */
std::uint32_t decode_one(const std::string& text, std::size_t& pos) {
  const unsigned char lead = static_cast<unsigned char>(text[pos]);
  if (lead < 0x80) {
    ++pos;
    return lead;
  }
  std::size_t extra = 0;
  std::uint32_t cp = 0;
  if ((lead & 0xE0) == 0xC0) {
    extra = 1;
    cp = lead & 0x1Fu;
  } else if ((lead & 0xF0) == 0xE0) {
    extra = 2;
    cp = lead & 0x0Fu;
  } else if ((lead & 0xF8) == 0xF0) {
    extra = 3;
    cp = lead & 0x07u;
  } else {
    ++pos;
    return 0xFFFDu;
  }
  if (pos + extra >= text.size()) {
    ++pos;
    return 0xFFFDu;
  }
  for (std::size_t i = 1; i <= extra; ++i) {
    const unsigned char tail = static_cast<unsigned char>(text[pos + i]);
    if ((tail & 0xC0) != 0x80) {
      ++pos;
      return 0xFFFDu;
    }
    cp = (cp << 6) | (tail & 0x3Fu);
  }
  pos += extra + 1;
  return cp;
}

}  // namespace

int text_width(const std::string& utf8) {
  int width = 0;
  std::size_t pos = 0;
  while (pos < utf8.size()) width += unicode_width(decode_one(utf8, pos));
  return width;
}

bool glob_match(const std::string& pattern, const std::string& text) {
  return glob_match(pattern.c_str(), text.c_str());
}

}  // namespace vd::gen
