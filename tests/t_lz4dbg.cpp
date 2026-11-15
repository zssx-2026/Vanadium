#include <cstdio>
#include <string>
#include "vanadium/crypto.h"
using namespace vd;
int main() {
  std::string sample;
  for (int i = 0; i < 40000; ++i) sample.push_back(static_cast<char>((i * 31 + (i / 97)) & 0xFF));
  std::string comp = lz4_compress(sample.data(), sample.size());
  std::printf("comp=%zu\n", comp.size());
  for (size_t i = 0; i < comp.size() && i < 40; ++i) std::printf("%02x ", (unsigned char)comp[i]);
  std::printf("\n");
  // show where literals 0..96 come from
  std::printf("sample[0..3]=%02x %02x %02x %02x\n", (unsigned char)sample[0], (unsigned char)sample[1], (unsigned char)sample[2], (unsigned char)sample[3]);
  std::printf("sample[97..100]=%02x %02x %02x %02x\n", (unsigned char)sample[97], (unsigned char)sample[98], (unsigned char)sample[99], (unsigned char)sample[100]);
  std::printf("sample[64..67]=%02x %02x %02x %02x\n", (unsigned char)sample[64], (unsigned char)sample[65], (unsigned char)sample[66], (unsigned char)sample[67]);
  return 0;
}
