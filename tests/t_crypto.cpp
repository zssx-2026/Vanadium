#include <cstdio>
#include <cstring>
#include <string>
#include "vanadium/crypto.h"

using namespace vd;

static int fails = 0;
static void check(bool ok, const char* what, const std::string& got, const std::string& want) {
  if (!ok) {
    ++fails;
    std::printf("FAIL %s\n  got  %s\n  want %s\n", what, got.c_str(), want.c_str());
  } else {
    std::printf("ok   %s\n", what);
  }
}

int main() {
  check(Sha256::hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256(abc)", Sha256::hex("abc", 3), "ba7816bf...");
  check(Sha256::hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256(empty)", Sha256::hex("", 0), "e3b0c442...");
  std::string big(1000000, 'a');
  check(Sha256::hex(big) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "sha256(1M a)", Sha256::hex(big), "cdc76e5c...");
  check(crc64_hex(crc64("123456789", 9)) == "995dc9bbdf1939fa", "crc64(xz)",
        crc64_hex(crc64("123456789", 9)), "995dc9bbdf1939fa");
  check(adler32("Wikipedia", 9) == 0x11E60398, "adler32",
        std::to_string(adler32("Wikipedia", 9)), "300286872");

  std::string sample;
  for (int i = 0; i < 40000; ++i) sample.push_back(static_cast<char>((i * 31 + (i / 97)) & 0xFF));
  std::string comp = lz4_compress(sample.data(), sample.size());
  std::string back;
  bool okdec = lz4_decompress(comp.data(), comp.size(), back, sample.size());
  check(okdec && back == sample, "lz4 roundtrip 40k", std::to_string(comp.size()), "n/a");

  std::string rep(50000, 'x');
  std::string comp2 = lz4_compress(rep.data(), rep.size());
  std::string back2;
  bool ok2 = lz4_decompress(comp2.data(), comp2.size(), back2, rep.size());
  check(ok2 && back2 == rep, "lz4 roundtrip repetitive",
        std::to_string(comp2.size()) + " vs " + std::to_string(rep.size()), "compressed");

  std::string b = base64_encode("hello world");
  std::string d;
  check(base64_decode(b, d) && d == "hello world", "base64", b, "aGVsbG8gd29ybGQ=");

  std::printf("FAILS=%d\n", fails);
  return fails ? 1 : 0;
}
