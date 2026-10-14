#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

#include "infra/system.h"

namespace akida {

struct RegDetail {
  uint32_t offset;
  uint32_t nb_bits;

  explicit constexpr RegDetail(uint32_t first, uint32_t last)
      : offset(first), nb_bits(last - first + 1) {
    // Minimum 1-bit field
    assert(first <= last);
    // Regiters are 32-bit
    assert(offset + nb_bits <= 32);
  }

  explicit constexpr RegDetail(uint32_t first) : offset(first), nb_bits(1) {}

  // Return the maximal value that can contrain the register field.
  constexpr uint32_t max_value() const {
    assert(nb_bits <= 32);
    return static_cast<uint32_t>((1ull << nb_bits) - 1);
  }
};

// Util function to set a range of bit to a value
inline void set_field(uint32_t* bits, const RegDetail& field, uint32_t value) {
  const auto max_val = field.max_value();
  if (value > max_val) {
    std::string message = "Attempted to write value " + std::to_string(value) +
                          " into a " + std::to_string(field.nb_bits) +
                          "-bit field, which will cause an overflow.";
    panic("%s", message.c_str());
  }

  // Mask value to avoid writing outside the field
  value &= max_val;
  // first clear bits
  *bits &= ~(max_val << field.offset);
  // Then set bits to value
  *bits |= value << field.offset;
}

inline uint32_t get_field(const uint32_t& bits, const RegDetail& field) {
  // create a mask
  const auto max_val = field.max_value();
  // shift and mask the value
  uint32_t ret = (bits >> field.offset) & max_val;
  return ret;
}

}  // namespace akida
