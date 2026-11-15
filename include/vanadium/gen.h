#pragma once
/* Interface implemented by generated code in src/gen. Everything here is produced by
   tools/codegen from the JSON schema files; the generator also emits the bulk tables. */
#include <cstdint>
#include <string>

namespace vd::gen {

/* Unicode: terminal display width of a code point (0, 1 or 2). */
int unicode_width(std::uint32_t cp);
/* Unicode general category as a 2-letter string ("Lu", "Zs", ...). */
const char* unicode_category(std::uint32_t cp);
/* Normalize a UTF-8 string to NFC (simplified: canonical composition for Latin/CJK). */
std::string unicode_normalize_nfc(const std::string& utf8);

/* Command metadata generated from schema/commands.json. */
int command_count();
const char* command_name(int index);
const char* command_summary(int index, int lang);
const char* command_usage(int index, int lang);
int command_option_count(int index);
const char* command_option(int cmd, int opt, int lang);
/* Flag and argument text of an option; cmd < 0 selects the global options. */
const char* command_option_flag(int cmd, int opt);
const char* command_option_arg(int cmd, int opt);

/* Error text for numeric error codes (schema/errors.json). */
const char* error_text(int code, int lang);
int error_count();

/* Self-test vectors: thousands of generated cases exercised by `inv self-test`. */
int self_test_count();
bool self_test_case(int index, std::string& name, std::string& detail);

/* Protocol codecs generated from schema/protocol.json. */
std::string proto_encode_field(int type_id, const std::string& value);
bool proto_decode_field(int type_id, const std::string& raw, std::string& value);

/* Reference tables. */
std::uint64_t crc64_table_entry(int index);
int unicode_table_codepoints();

/* Display width of a UTF-8 string in terminal cells (generated width tables). */
int text_width(const std::string& utf8);

/* Glob match with '*', '?' and '[...]' over a whole string. */
bool glob_match(const char* pattern, const char* text);
bool glob_match(const std::string& pattern, const std::string& text);

/* Canonicalize a path: '\\' -> '/', drop '.' segments, fold '..' and empty parts. */
std::string normalize_path(const std::string& path);

/* Validate one setting assignment against the generated settings schema.
   Returns 0 when accepted, otherwise the generated error code. */
int validate_setting(const std::string& key, const std::string& value);

/* Run every generated self-test vector; returns the total, fills pass/fail counts. */
int self_test_run(int* passed, int* failed);

/* Unicode names and general-category ids from the generated tables. */
int unicode_name_count();
const char* unicode_name(std::uint32_t cp);
int unicode_category_id(std::uint32_t cp);

/* Protocol type registry generated from schema/protocol.json. */
int proto_type_count();
const char* proto_type_name(int type_id);

}  // namespace vd::gen
