#include "pico_conversion.h"

#include "infra/int_ops.h"

namespace akida::conversion {

Shape get_aligned_shape(const Shape& shape, size_t type_size) {
  // Calculate the alignment factor (32 bits = 4 bytes).
  const auto alignment_factor = static_cast<uint32_t>(4U / type_size);

  // If already aligned, return the original shape.
  if (alignment_factor <= 1) {
    return shape;
  }

  // Create a new shape with channels aligned to 32-bit.
  auto channels = shape[2];
  auto aligned_channels = align_up(channels, alignment_factor);
  std::vector<Index> new_dims(shape.begin(), shape.end());
  new_dims[2] = static_cast<Index>(aligned_channels);
  return Shape(new_dims);
}

Shape get_unaligned_shape(const Shape& aligned_shape,
                          uint32_t original_channels) {
  // If original_channels equals aligned channels, return as-is
  if (original_channels >= aligned_shape[2]) {
    return aligned_shape;
  }

  // Create a new shape with the original (unaligned) channels.
  std::vector<Index> new_dims(aligned_shape.begin(), aligned_shape.end());
  new_dims[2] = static_cast<Index>(original_channels);
  return Shape(new_dims);
}

const Dense* align_channels_to_32b(const Dense* input) {
  const auto shape = input->dimensions();
  const auto type = input->type();
  const auto layout = input->layout();
  const auto type_size = tensor_type_size(type);
  const auto aligned_shape = get_aligned_shape(shape, type_size);

  // If the input is already aligned, return it.
  if (aligned_shape[2] == shape[2]) {
    return input;
  }

  auto result = Dense::create(type, aligned_shape, layout);
  const auto* src_data = input->buffer()->data();
  auto* dst_data = result->buffer()->data();

  // Copy the channels from input and pad with zeros to align to 32-bit.
  auto channels = shape[2];
  auto aligned_channels = aligned_shape[2];
  auto num_rows = shape[0] * shape[1];
  for (size_t t = 0; t < num_rows; ++t) {
    auto src_offset = t * channels * type_size;
    auto dst_offset = t * aligned_channels * type_size;
    std::memcpy(dst_data + dst_offset, src_data + src_offset,
                type_size * channels);
  }

  // Transfer ownership of the tensor from the unique_ptr to the caller.
  return result.release();
}

TensorUniquePtr unaligned_channels_from_32b(TensorUniquePtr input,
                                            uint32_t original_channels) {
  auto* dense_input = input->is_dense_tensor() ? static_cast<Dense*>(input.get())
                                            : nullptr;
  if (dense_input == nullptr) {
    panic("Input must be a Dense tensor");
  }
  const auto aligned_shape = dense_input->dimensions();
  const auto aligned_channels = aligned_shape[2];
  const auto type = dense_input->type();
  const auto layout = dense_input->layout();
  const auto type_size = tensor_type_size(type);
  const auto unaligned_shape =
      get_unaligned_shape(aligned_shape, original_channels);

  // Calculate the alignment factor (32 bits = 4 bytes).
  const auto alignment_factor = static_cast<uint32_t>(4U / type_size);

  // If no unalignment is needed, return the input.
  if (alignment_factor <= 1 || unaligned_shape[2] == aligned_channels) {
    return input;
  }

  auto result = Dense::create(type, unaligned_shape, layout);
  const auto* src_data = dense_input->buffer()->data();
  auto* dst_data = result->buffer()->data();

  // Pico aligns the channels from output to 32-bit.
  // Copy timestep by timestep, skipping padding at the end of each timestep.
  for (size_t t = 0; t < aligned_shape[1]; ++t) {
    const auto src_offset = t * aligned_channels * type_size;
    const auto dst_offset = t * original_channels * type_size;
    std::memcpy(dst_data + dst_offset, src_data + src_offset,
                original_channels * type_size);
  }

  return result;
}

}  // namespace akida::conversion
