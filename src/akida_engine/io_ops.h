#pragma once

#include <algorithm>
#include <cmath>
#include "akida/dense.h"
#include "akida/program_info.h"

namespace akida::ops {

template<typename T1, typename T2>
DensePtr quantize(const DenseConstPtr& inputs,
                  const ProgramInfo& program_info) {
  // Assertion on the type of parameters.
  if (inputs->dimensions().size() != 3 &&
      inputs->type() != TensorType::float32) {
    panic("Quantize expects a 3D float input");
  }

  DensePtr transposed_inputs;
  if (program_info.input_channels_first()) {
    transposed_inputs = inputs->transpose<T1>({1, 2, 0});
  }

  const auto input_dims = program_info.input_channels_first()
                              ? transposed_inputs->dimensions()
                              : inputs->dimensions();
  const auto input_size = inputs->size();
  const auto last_dim = input_dims[input_dims.size() - 1];
  TensorType output_type = akida::TensorType::uint8;
  if constexpr (std::is_same<T2, int8_t>::value) {
    output_type = akida::TensorType::int8;
  }

  // Create output shape.
  auto output = Dense::create(output_type, input_dims, Dense::Layout::RowMajor);

  // shift/scales is broadcasted on the last dimension of input

  const auto* scale_p = program_info.input_scales().data;
  const auto* zero_point_p = program_info.input_zero_points().data;

  const auto* in_p = program_info.input_channels_first()
                         ? transposed_inputs->data<T1>()
                         : inputs->data<T1>();
  auto out_p = output->template data<T2>();

  // loop through all the elements.
  for (size_t i = 0; i < input_size;) {
    for (Index j = 0; j < last_dim; j++, i++) {
      int32_t out_val{};
      if constexpr (std::is_same<T1, float>::value) {
        out_val = static_cast<int32_t>(std::round(in_p[i] * scale_p[j]));
        out_val += zero_point_p[j];
      } else {
        out_val = in_p[i];
      }
      if (program_info.input_bits() == 4) {
        out_val = std::min<int32_t>(std::max<int32_t>(out_val, 0), 15);
      } else if (program_info.input_sign()) {
        out_val = std::min<int32_t>(std::max<int32_t>(out_val, -128), 127);
      } else {
        out_val = std::min<int32_t>(std::max<int32_t>(out_val, 0), 255);
      }
      out_p[i] = static_cast<T2>(out_val);
    }
  }
  return output;
}

inline DensePtr quantize(const DenseConstPtr& inputs,
                         const ProgramInfo& program_info) {
  if (inputs->layout() != Dense::Layout::RowMajor) {
    panic("Quantize expects a RowMajor Dense");
  }
  if (program_info.input_sign()) {
    if (inputs->type() == TensorType::float32) {
      return quantize<float, int8_t>(inputs, program_info);
    }
    return quantize<int8_t, int8_t>(inputs, program_info);
  }
  if (inputs->type() == TensorType::float32) {
    return quantize<float, uint8_t>(inputs, program_info);
  }
  return quantize<uint8_t, uint8_t>(inputs, program_info);
}

template<typename T>
DenseUniquePtr dequantize(const Dense& potentials,
                          const ProgramInfo& program_info) {
  // Get potentials strides and data from program.
  auto shifts = program_info.output_shift();
  auto scales = program_info.output_scales();
  assert(shifts.size == scales.size);
  const auto& shift = shifts.data;
  const auto& scale = scales.data;

  // Perform sanity checks.
  const auto coords = potentials.dimensions();
  if (coords.size() != 3) {
    panic("Dequantize expects a 3D Dense");
  }

  // Get potentials strides and data to access them via linear index.
  const auto pot_strides = potentials.strides();
  const auto pot_data = potentials.data<T>();
  // Allocate a dense output in the form of a RowMajor Tensor.
  auto rescaled_outputs =
      Dense::create(TensorType::float32, coords, Dense::Layout::RowMajor);
  // Get rescaled outputs data
  auto* resc_data = rescaled_outputs->data<float>();
  for (Index x = 0; x < coords[0]; x++) {
    for (Index y = 0; y < coords[1]; y++) {
      // Move pointer at the beginning of the neuron.
      Index coord_n0[] = {x, y, 0};
      auto coord_lin_index_n0 = linear_index(coord_n0, pot_strides);
      auto poti = &pot_data[coord_lin_index_n0];
      auto resci = &resc_data[coord_lin_index_n0];
      for (Index n = 0; n < coords[2]; n++) {
        // Evaluate rescaled output
        auto value = static_cast<float>(poti[n] - shift[n]) * scale[n];
        // Set rescaled value at the same index than output.
        resci[n] = value;
      }
    }
  }
  return rescaled_outputs;
}

inline DenseUniquePtr dequantize(const Dense& potentials,
                                 const ProgramInfo& program_info) {
  if (potentials.layout() != Dense::Layout::RowMajor) {
    panic("Dequantize expects a RowMajor Dense");
  }
  switch (potentials.type()) {
    case TensorType::int32:
      return dequantize<int32_t>(potentials, program_info);
    case TensorType::int16:
      return dequantize<int16_t>(potentials, program_info);
    case TensorType::int8:
      return dequantize<int8_t>(potentials, program_info);
    default:
      panic("Dequantize expects an int32/int16/int8 Dense");
  }
}
}  // namespace akida::ops
