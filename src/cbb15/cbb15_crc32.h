// BrainBoard1500 - CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320).
// Same checksum as Python's zlib.crc32(), which the export tool uses.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cbb15 {

// Continues a running checksum; start with crc = 0.
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t size);

inline uint32_t crc32(const uint8_t* data, size_t size) {
  return crc32Update(0, data, size);
}

}  // namespace cbb15
