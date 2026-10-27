#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#include "akida/shape.h"
#include "akida/tensor.h"
#include "infra/exports.h"

/** file akida/dense.h
 * Contains the abstract Dense object and its related types
 */

namespace akida {

/**
 * @brief A shared pointer to a Dense object
 */
using DensePtr = std::shared_ptr<Dense>;

/**
 * @brief A shared pointer to a const Dense object
 */
using DenseConstPtr = std::shared_ptr<const Dense>;

/**
 * @brief A unique pointer to a Dense object
 */
using DenseUniquePtr = std::unique_ptr<Dense>;

/**
 * class Dense
 *
 * An abstraction of a multi-dimensional dense array
 *
 * Stores data using a column-major (default) or row-major layout
 *
 * To iterate over the values of a Dense of type T, one would typically call
 * the data<T>() templated member.
 *
 */
class AKIDASHAREDLIB_EXPORT Dense : public Tensor {
 public:
  // BrainBoard1500 patch (RTTI-free type query), see PATCHES.md
  bool is_dense_tensor() const override { return true; }

  virtual ~Dense() {}

  bool operator==(const Tensor& ref) const override;

  bool operator==(const Dense& ref) const {
    return type() == ref.type() && layout() == ref.layout() &&
           dimensions() == ref.dimensions() && size() == ref.size() &&
           std::memcmp(buffer()->data(), ref.buffer()->data(),
                       buffer()->size()) == 0;
  }

  /**
   * @enum  Layout
   * @brief The Dense memory layout (storage order)
   * The memory layout of a Dense tensor has an impact on how the linear index
   * of the elements is calculated from each element coordinates.
   * For a row-major Dense, when you increment the first coordinate, the element
   * index is incremented by a factor corresponding to the product of all
   * dimensions but the first one.
   * On the contrary, for a column-major Dense, when you increment the first
   * coordinate, the index is just incremented by one.
   */
  enum class Layout {
    RowMajor /**<RowMajor, or 'biggest stride first'*/,
    ColMajor /**<ColMajor, or 'smallest stride first'*/
  };

  /**
   * @brief returns the Dense tensor layout
   * @return : Layout::ColMajor or Layout::RowMajor
   */
  virtual Layout layout() const = 0;

  /**
   * @brief returns the Dense strides for each dimension
   * @return : a vector of strides for direct access to the tensor data
   */
  virtual const std::vector<uint32_t>& strides() const = 0;

  /**
   * @brief Modifies a Dense by setting a new shape
   * The shape of the dense is modified, its data is untouched, but a new shape
   * is set. New shape should contain the same number of dimensions as the old
   * one, and the product of its dimensions should be the same as the product of
   * the old one.
   * @param : the new shape
   */
  virtual void reshape(const Shape& new_shape) = 0;

  /**
   * @brief Get the value at the specified coordinates
   * @param coords : the set of coordinates
   * @return the Dense value at these coordinates
   */
  template<typename T>
  T get(const std::vector<Index>& coords) const {
    auto index = linear_index(coords, strides());
    if (index > size() - 1) {
      panic("Coordinates are out-of-range (linear index %zu, tensor size %zu)",
            index, size());
    }
    return data<T>()[index];
  }

  /**
   * @brief Set the value at the specified coordinates
   * @param coords : the set of coordinates
   * @param value : the value at these coordinates
   */
  template<typename T>
  void set(const std::vector<Index>& coords, T value) {
    auto index = linear_index(coords, strides());
    if (index > size() - 1) {
      panic("Coordinates are out-of-range (linear index %zu, tensor size %zu)",
            index, size());
    }
    data<T>()[index] = value;
  }

  /**
   * @brief Set the same value at all coordinates
   * @param value : the value
   */
  template<typename T>
  void fill(T value) {
    auto cached_size = size();
    for (size_t i = 0; i < cached_size; ++i) {
      data<T>()[i] = value;
    }
  }

  /**
   * @brief Returns the strides corresponding to the given shape
   * @param shape : the Dense tensor dimensions
   * @param layout : the Dense tensor layout
   * @return : a vector of strides for direct access to the tensor data
   */
  static std::vector<uint32_t> eval_strides(const Shape& shape, Layout layout);

  /**
   * @brief Create a Dense, allocating its internal buffer (initialized to 0)
   *
   * @param type   : the Tensor data type, as an akida::TensorType
   * @param dims   : the Tensor dimensions
   * @param layout : the source array layout, can be one of akida::RowMajor,
   * akida::ColMajor
   */
  static DenseUniquePtr create(TensorType type, const Shape& dims,
                               Dense::Layout layout);

  /**
   * @brief Create a Dense, allocating its internal buffer and copy the array
   * argument into it
   * @param array      : a pointer to the source byte array
   * @param bytes_size : the array size in bytes
   * @param type       : the Tensor data type, as an akida::TensorType
   * @param dims       : the Tensor dimensions
   * @param layout     : the source array layout, can be one of akida::RowMajor,
   * akida::ColMajor
   */
  static DenseUniquePtr copy(const char* array, size_t bytes_size,
                             TensorType type, const Shape& dims,
                             Dense::Layout layout);

  /**
   * @brief Create a Dense from Sparse
   * @param sparse : the Sparse object to clone
   */
  static DenseUniquePtr from_sparse(const Sparse& sparse, Layout layout);

  /**
   * @brief Create a Dense view (a Dense object that does not own the buffer)
   * from a byte array. No internal allocation is done.
   * @param array  : a pointer to the source byte array
   * @param type   : the Tensor data type, as an akida::TensorType
   * @param dims   : the Tensor dimensions
   * @param layout : the source array layout, can be one of akida::RowMajor,
   * akida::ColMajor
   */
  static DenseUniquePtr create_view(const char* array, TensorType type,
                                    const Shape& dims, Dense::Layout layout);

  /**
   * @brief Splits a Dense along longest stride axis
   * The resulting sub-tensors might be just a view on the original data,
   * meaning that they will not own their buffer and that original buffer should
   * not be deleted while the subtensors are in use.
   * @param : the tensor to split
   * @return : a vector of sub-tensors
   */
  static std::vector<TensorConstPtr> split(const Dense& t);

  /**
   * @brief Transpose a Dense
   * @param : a vector of permutations of the dimensions
   * @return : a transposed Dense
   */
  template<typename T>
  DensePtr transpose(const std::vector<Index>& permutations) const {
    const auto old_shape = dimensions();
    if (permutations.size() != 3 || old_shape.size() != 3) {
      // BrainBoard1500 patch: was a throw (no exceptions), see PATCHES.md
      panic("Dense::transpose only supports 3D tensors.");
    }
    std::vector<Index> new_dims(permutations.size());
    for (size_t i = 0; i < permutations.size(); ++i) {
      new_dims[i] = old_shape[permutations[i]];
    }
    const Shape new_shape(new_dims);

    auto output = Dense::create(type(), new_shape, layout());

    const auto in_strides = strides();
    const auto out_strides = output->strides();

    const auto* in_p = data<T>();
    auto* out_p = output->data<T>();

    for (Index i0 = 0; i0 < old_shape[0]; ++i0) {
      for (Index i1 = 0; i1 < old_shape[1]; ++i1) {
        for (Index i2 = 0; i2 < old_shape[2]; ++i2) {
          const size_t in_offset = (i0 * in_strides[0]) + (i1 * in_strides[1]) +
                                   (i2 * in_strides[2]);
          const std::array<Index, 3> in_idx = {i0, i1, i2};
          const std::array<Index, 3> out_idx = {in_idx[permutations[0]],
                                                in_idx[permutations[1]],
                                                in_idx[permutations[2]]};
          size_t out_offset = (out_idx[0] * out_strides[0]) +
                              (out_idx[1] * out_strides[1]) +
                              (out_idx[2] * out_strides[2]);
          out_p[out_offset] = in_p[in_offset];
        }
      }
    }

    return output;
  }
};

}  // namespace akida
