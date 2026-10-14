#include "akida/tensor.h"

#include <memory>

#include "akida/dense.h"
#include "akida/sparse.h"

namespace akida {

DenseConstPtr Tensor::as_dense(TensorConstPtr tensor) {
  return (tensor && tensor->is_dense_tensor())
             ? std::static_pointer_cast<const Dense>(tensor)
             : nullptr;  // BrainBoard1500 patch: was dynamic_pointer_cast
}

SparseConstPtr Tensor::as_sparse(TensorConstPtr tensor) {
  return (tensor && tensor->is_sparse_tensor())
             ? std::static_pointer_cast<const Sparse>(tensor)
             : nullptr;  // BrainBoard1500 patch: was dynamic_pointer_cast
}

DenseConstPtr Tensor::ensure_dense(TensorConstPtr tensor) {
  // Assume this is already a Dense
  auto dense = Tensor::as_dense(tensor);
  if (dense) {
    return dense;
  }
  // If we were passed a Sparse, convert it to a Dense
  auto sparse = (tensor && tensor->is_sparse_tensor())
                    ? std::static_pointer_cast<const Sparse>(tensor)
                    : nullptr;  // BrainBoard1500 patch: was dynamic_pointer_cast
  if (sparse) {
    return Dense::from_sparse(*sparse, Dense::Layout::RowMajor);
  }
  return nullptr;
}

}  // namespace akida
