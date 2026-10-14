#pragma once

#include "hw_version.h"

namespace akida::device_capability {

/**
 * @brief Partial reconfiguration feature
 * @param hw_version The hardware version to check
 * @return if the feature is supported.
 */
inline bool supports_hwpr(const HwVersion& hw_version) {
  return hw_version.product_id >= 0xA1;
}

/**
 * @brief Sparse inputs feature
 * @param hw_version The hardware version to check
 * @return if the feature is supported.
 */
inline bool supports_sparse_inputs(const HwVersion& hw_version) {
  return hw_version.product_id == 0;
}

/**
 * @brief Dense outputs feature
 * @param hw_version The hardware version to check
 * @return if the feature is supported.
 */
inline bool supports_dense_outputs(const HwVersion& hw_version) {
  return hw_version.product_id >= 0xA1;
}

/**
 * @brief Hrc feature
 * @param hw_version The hardware version to check
 * @return if the feature is supported.
 */
inline bool supports_hrc(const HwVersion& hw_version) {
  return hw_version.get_ip_version() != IpVersion::pico;
}
}  // namespace akida::device_capability
