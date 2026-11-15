#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "core.h"

namespace vd {

/* SHA-256, FIPS 180-4. Streaming + one-shot. */
class Sha256 {
 public:
  Sha256();
  void update(const void* data, std::size_t len);
  std::string final_hex();
  static std::string hex(const void* data, std::size_t len);
  static std::string hex(const std::string& s) { return hex(s.data(), s.size()); }

 private:
  void block(const u8* p);
  u32 h_[8];
  u64 len_ = 0;
  u8 buf_[64];
  std::size_t fill_ = 0;
};

/* CRC-64/XZ (ECMA-182 reflected, poly 0x42F0E1EBA9EA3693, init/xorout all ones). */
u64 crc64(const void* data, std::size_t len, u64 seed = 0);
u64 crc64_file(const std::string& path);
std::string crc64_hex(u64 v);

/* LZ4 block codec (raw block format, no frame). */
std::string lz4_compress(const void* data, std::size_t len);
bool lz4_decompress(const void* src, std::size_t src_len, std::string& out,
                    std::size_t expected_len);

/* Adler-32 (used in pack headers). */
u32 adler32(const void* data, std::size_t len);

/* Base64. */
std::string base64_encode(const std::string& raw);
bool base64_decode(const std::string& text, std::string& out);

/* Assembler-accelerated primitives (src/asm/*.asm). C++ fallbacks live in crypto.cpp. */
extern "C" {
std::uint64_t vd_crc64_asm(const void* data, std::size_t len, std::uint64_t seed);
void vd_sha256_block_asm(std::uint32_t state[8], const std::uint8_t block[64]);
}

}  // namespace vd
