// Self-test driver for generated code. Not part of the library.
#include <cstdio>
#include <string>

#include "gen_internal.h"
#include "vanadium/gen.h"

int main() {
  int passed = 0;
  int failed = 0;
  const int total = vd::gen::self_test_run(&passed, &failed);
  std::printf("SELFTEST total=%d passed=%d failed=%d\n", total, passed, failed);
  if (failed != 0) {
    int shown = 0;
    for (int i = 0; i < total && shown < 5; ++i) {
      std::string name;
      std::string detail;
      if (vd::gen::self_test_case(i, name, detail)) {
        std::printf("FAILCASE %s %s\n", name.c_str(), detail.c_str());
        ++shown;
      }
    }
  }
  std::printf("WIDTH_A=%d WIDTH_HAN=%d WIDTH_EMOJI=%d\n",
              vd::gen::unicode_width(0x41), vd::gen::unicode_width(0x4E00),
              vd::gen::unicode_width(0x1F600));
  std::printf("CAT_A=%s CAT_HAN=%s\n", vd::gen::unicode_category(0x41),
              vd::gen::unicode_category(0x4E00));
  std::printf("COUNTS cmds=%d errors=%d cps=%d msgs=%d types=%d objs=%d\n",
              vd::gen::command_count(), vd::gen::error_count(),
              vd::gen::unicode_table_codepoints(), vd::gen::msg_catalog_count(),
              vd::gen::proto_type_count(), vd::gen::proto_object_count());
  std::printf("NAME_0041=%s\n", vd::gen::unicode_name(0x41) ? vd::gen::unicode_name(0x41) : "(null)");
  const char* zh = vd::gen::msg_lookup("conn.connecting", 1);
  const char* missing = vd::gen::msg_lookup("no.such.key", 0);
  std::printf("MSG_ZH_NULL=%d MSG_MISS_NULL=%d\n", zh == nullptr ? 1 : 0, missing == nullptr ? 1 : 0);
  std::printf("CMD0=%s OPT0=%s\n", vd::gen::command_name(0),
              vd::gen::command_option(0, 0, 0));
  const std::string enc = vd::gen::proto_encode_field(2, "4660");
  std::string dec;
  const bool ok = vd::gen::proto_decode_field(2, enc, dec);
  std::printf("PROTO_U16 enc_len=%zu dec=%s ok=%d\n", enc.size(), dec.c_str(), ok ? 1 : 0);
  std::printf("NFC=%s\n", vd::gen::unicode_normalize_nfc("e\xCC\x81").c_str());
  std::printf("CRC64_0=%016llx\n", static_cast<unsigned long long>(vd::gen::crc64_table_entry(1)));
  return failed == 0 ? 0 : 2;
}
