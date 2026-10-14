#include "cbb15_crc32.h"

namespace cbb15 {

namespace {

// 16-entry nibble table: small and fast enough for flash verification.
const uint32_t kNibbleTable[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
    0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
    0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};

}  // namespace

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t size) {
  crc = ~crc;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    crc = (crc >> 4) ^ kNibbleTable[crc & 0x0F];
    crc = (crc >> 4) ^ kNibbleTable[crc & 0x0F];
  }
  return ~crc;
}

}  // namespace cbb15
