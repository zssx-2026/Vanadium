#include <cstring>
#include <vector>

#include "vanadium/crypto.h"

namespace vd {

/* ------------------------------------------------------------------ SHA-256 */

namespace {
const u32 kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline u32 rotr(u32 x, int n) { return (x >> n) | (x << (32 - n)); }
}  // namespace

Sha256::Sha256() {
  h_[0] = 0x6a09e667u;
  h_[1] = 0xbb67ae85u;
  h_[2] = 0x3c6ef372u;
  h_[3] = 0xa54ff53au;
  h_[4] = 0x510e527fu;
  h_[5] = 0x9b05688cu;
  h_[6] = 0x1f83d9abu;
  h_[7] = 0x5be0cd19u;
}

void Sha256::block(const u8* p) {
  u32 w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<u32>(p[i * 4]) << 24) | (static_cast<u32>(p[i * 4 + 1]) << 16) |
           (static_cast<u32>(p[i * 4 + 2]) << 8) | static_cast<u32>(p[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  u32 a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
  for (int i = 0; i < 64; ++i) {
    u32 s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    u32 ch = (e & f) ^ (~e & g);
    u32 t1 = h + s1 + ch + kSha256K[i] + w[i];
    u32 s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    u32 maj = (a & b) ^ (a & c) ^ (b & c);
    u32 t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
  h_[5] += f;
  h_[6] += g;
  h_[7] += h;
}

void Sha256::update(const void* data, std::size_t len) {
  const u8* p = static_cast<const u8*>(data);
  len_ += len;
  if (fill_) {
    std::size_t need = 64 - fill_;
    std::size_t take = len < need ? len : need;
    std::memcpy(buf_ + fill_, p, take);
    fill_ += take;
    p += take;
    len -= take;
    if (fill_ == 64) {
      block(buf_);
      fill_ = 0;
    }
  }
  while (len >= 64) {
    block(p);
    p += 64;
    len -= 64;
  }
  if (len) {
    std::memcpy(buf_, p, len);
    fill_ = len;
  }
}

std::string Sha256::final_hex() {
  u64 bits = len_ * 8;
  u8 pad = 0x80;
  update(&pad, 1);
  u8 zero = 0;
  while (fill_ != 56) update(&zero, 1);
  u8 lenbuf[8];
  for (int i = 0; i < 8; ++i) lenbuf[i] = static_cast<u8>((bits >> (56 - i * 8)) & 0xFF);
  update(lenbuf, 8);
  u8 out[32];
  for (int i = 0; i < 8; ++i) {
    out[i * 4] = static_cast<u8>(h_[i] >> 24);
    out[i * 4 + 1] = static_cast<u8>(h_[i] >> 16);
    out[i * 4 + 2] = static_cast<u8>(h_[i] >> 8);
    out[i * 4 + 3] = static_cast<u8>(h_[i]);
  }
  return hex_encode(out, 32);
}

std::string Sha256::hex(const void* data, std::size_t len) {
  Sha256 h;
  h.update(data, len);
  return h.final_hex();
}

/* -------------------------------------------------------------------- CRC64 */

namespace {
struct Crc64Table {
  u64 t[256];
  Crc64Table() {
    // Reflected CRC-64/XZ polynomial (bit-reversed 0x42F0E1EBA9EA3693).
    const u64 poly = 0xC96C5795D7870F42ULL;
    for (int i = 0; i < 256; ++i) {
      u64 crc = static_cast<u64>(i);
      for (int k = 0; k < 8; ++k)
        crc = (crc & 1) ? (crc >> 1) ^ poly : (crc >> 1);
      t[i] = crc;
    }
  }
};
const Crc64Table kCrc64Table;
}  // namespace

u64 crc64(const void* data, std::size_t len, u64 seed) {
  u64 crc = ~seed;
  const u8* p = static_cast<const u8*>(data);
  for (std::size_t i = 0; i < len; ++i)
    crc = kCrc64Table.t[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

u64 crc64_file(const std::string& path) {
  std::string data;
  if (!fs::read_file_opt(path, data)) return 0;
  return crc64(data.data(), data.size());
}

std::string crc64_hex(u64 v) {
  char buf[17];
  for (int i = 0; i < 16; ++i) {
    int nib = static_cast<int>((v >> (60 - i * 4)) & 0xF);
    buf[i] = static_cast<char>(nib < 10 ? '0' + nib : 'a' + nib - 10);
  }
  buf[16] = '\0';
  return std::string(buf, 16);
}

/* ---------------------------------------------------------------- Adler-32 */

u32 adler32(const void* data, std::size_t len) {
  const u8* p = static_cast<const u8*>(data);
  u32 a = 1, b = 0;
  std::size_t i = 0;
  while (i < len) {
    std::size_t chunk = (len - i) < 5552 ? (len - i) : 5552;
    for (std::size_t k = 0; k < chunk; ++k) {
      a += p[i + k];
      b += a;
    }
    a %= 65521;
    b %= 65521;
    i += chunk;
  }
  return (b << 16) | a;
}

/* -------------------------------------------------------------------- LZ4 */

namespace {
inline u32 read_le32(const u8* p) {
  return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
         (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
inline u32 hash4(u32 v) { return (v * 2654435761u) >> (32 - 12); }
}  // namespace

/* Greedy LZ4 block compressor (16-bit offsets, 4-byte hash table). */
std::string lz4_compress(const void* data, std::size_t len) {
  const u8* src = static_cast<const u8*>(data);
  std::string out;
  out.reserve(len + len / 255 + 16);
  if (len < 13) {
    std::size_t n = len;
    if (n >= 15) {
      out.push_back(static_cast<char>(0xF0));
      std::size_t rem = n - 15;
      while (rem >= 255) {
        out.push_back(static_cast<char>(255));
        rem -= 255;
      }
      out.push_back(static_cast<char>(rem));
    } else {
      out.push_back(static_cast<char>(n << 4));
    }
    out.append(reinterpret_cast<const char*>(src), len);
    return out;
  }
  std::vector<int> table(4096, -1);
  std::size_t anchor = 0;
  std::size_t i = 0;
  const std::size_t mflimit = len - 12;
  auto emit_sequence = [&](std::size_t lit_start, std::size_t lit_len, std::size_t match_len,
                           std::size_t offset) {
    u8 token = 0;
    if (lit_len >= 15) token |= 0xF0;
    else token |= static_cast<u8>(lit_len << 4);
    std::size_t ml = match_len - 4;
    if (ml >= 15) token |= 0x0F;
    else token |= static_cast<u8>(ml);
    out.push_back(static_cast<char>(token));
    if (lit_len >= 15) {
      std::size_t rem = lit_len - 15;
      while (rem >= 255) {
        out.push_back(static_cast<char>(255));
        rem -= 255;
      }
      out.push_back(static_cast<char>(rem));
    }
    out.append(reinterpret_cast<const char*>(src + lit_start), lit_len);
    out.push_back(static_cast<char>(offset & 0xFF));
    out.push_back(static_cast<char>((offset >> 8) & 0xFF));
    if (ml >= 15) {
      std::size_t rem = ml - 15;
      while (rem >= 255) {
        out.push_back(static_cast<char>(255));
        rem -= 255;
      }
      out.push_back(static_cast<char>(rem));
    }
  };

  while (i < mflimit) {
    u32 seq = read_le32(src + i);
    u32 h = hash4(seq);
    int cand = table[h];
    table[h] = static_cast<int>(i);
    if (cand >= 0 && static_cast<std::size_t>(cand) < i &&
        i - static_cast<std::size_t>(cand) <= 65535 && read_le32(src + cand) == seq) {
      std::size_t match = 4;
      while (i + match < len && src[cand + match] == src[i + match]) ++match;
      emit_sequence(anchor, i - anchor, match, i - static_cast<std::size_t>(cand));
      i += match;
      anchor = i;
      if (i < mflimit) table[hash4(read_le32(src + i))] = static_cast<int>(i);
    } else {
      ++i;
    }
  }
  if (anchor < len) {
    std::size_t lit = len - anchor;
    u8 token = lit >= 15 ? 0xF0 : static_cast<u8>(lit << 4);
    out.push_back(static_cast<char>(token));
    if (lit >= 15) {
      std::size_t rem = lit - 15;
      while (rem >= 255) {
        out.push_back(static_cast<char>(255));
        rem -= 255;
      }
      out.push_back(static_cast<char>(rem));
    }
    out.append(reinterpret_cast<const char*>(src + anchor), lit);
  }
  return out;
}

bool lz4_decompress(const void* src_raw, std::size_t src_len, std::string& out,
                    std::size_t expected_len) {
  const u8* src = static_cast<const u8*>(src_raw);
  out.clear();
  out.reserve(expected_len ? expected_len : src_len * 3);
  std::size_t i = 0;
  while (i < src_len) {
    u8 token = src[i++];
    std::size_t lit = token >> 4;
    if (lit == 15) {
      while (i < src_len) {
        u8 b = src[i++];
        lit += b;
        if (b != 255) break;
      }
    }
    if (i + lit > src_len) return false;
    out.append(reinterpret_cast<const char*>(src + i), lit);
    i += lit;
    if (i >= src_len) break;
    if (i + 2 > src_len) return false;
    std::size_t offset =
        static_cast<std::size_t>(src[i]) | (static_cast<std::size_t>(src[i + 1]) << 8);
    i += 2;
    if (offset == 0 || offset > out.size()) return false;
    std::size_t ml = token & 0x0F;
    if (ml == 15) {
      while (i < src_len) {
        u8 b = src[i++];
        ml += b;
        if (b != 255) break;
      }
    }
    ml += 4;
    std::size_t start = out.size() - offset;
    for (std::size_t k = 0; k < ml; ++k) out.push_back(out[start + k]);
  }
  if (expected_len && out.size() != expected_len) return false;
  return true;
}

/* ------------------------------------------------------------------ Base64 */

std::string base64_encode(const std::string& raw) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((raw.size() + 2) / 3 * 4);
  std::size_t i = 0;
  const u8* p = reinterpret_cast<const u8*>(raw.data());
  while (i + 2 < raw.size()) {
    u32 v = (static_cast<u32>(p[i]) << 16) | (static_cast<u32>(p[i + 1]) << 8) | p[i + 2];
    out.push_back(tbl[(v >> 18) & 0x3F]);
    out.push_back(tbl[(v >> 12) & 0x3F]);
    out.push_back(tbl[(v >> 6) & 0x3F]);
    out.push_back(tbl[v & 0x3F]);
    i += 3;
  }
  if (i + 1 == raw.size()) {
    u32 v = static_cast<u32>(p[i]) << 16;
    out.push_back(tbl[(v >> 18) & 0x3F]);
    out.push_back(tbl[(v >> 12) & 0x3F]);
    out += "==";
  } else if (i + 2 == raw.size()) {
    u32 v = (static_cast<u32>(p[i]) << 16) | (static_cast<u32>(p[i + 1]) << 8);
    out.push_back(tbl[(v >> 18) & 0x3F]);
    out.push_back(tbl[(v >> 12) & 0x3F]);
    out.push_back(tbl[(v >> 6) & 0x3F]);
    out.push_back('=');
  }
  return out;
}

bool base64_decode(const std::string& text, std::string& out) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
  };
  out.clear();
  u32 acc = 0;
  int bits = 0;
  for (char c : text) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    int v = val(c);
    if (v < 0) return false;
    acc = (acc << 6) | static_cast<u32>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((acc >> bits) & 0xFF));
    }
  }
  return true;
}

}  // namespace vd
