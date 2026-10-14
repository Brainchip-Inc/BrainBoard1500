#pragma once
#include "akida/dense.h"

namespace akida::conversion {

Shape get_aligned_shape(const Shape& shape, size_t type_size);

Shape get_unaligned_shape(const Shape& aligned_shape,
                          uint32_t original_channels);

const Dense* align_channels_to_32b(const Dense* input);

TensorUniquePtr unaligned_channels_from_32b(TensorUniquePtr input,
                                            uint32_t original_channels);

}  // namespace akida::conversion
