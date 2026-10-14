#pragma once
#include <cstdint>
#include <vector>
#include "dma_engine.h"
#include "memory_mgr.h"

namespace akida {
namespace dma {

using Descriptor = std::vector<uint32_t>;

Descriptor format_config_desc(bool direction, uint32_t input_addr,
                              uint32_t output_addr, uint32_t buf_sz);
// constants used for config formatting
inline constexpr bool kDescConfigDirectionWrite = true;
inline constexpr bool kDescConfigDirectionRead = false;

Descriptor format_input_desc(const dma::Input& input_dma, uint32_t job_id,
                             uint32_t input_addr, uint32_t buffer_size,
                             uint32_t output_addr, uint32_t learning_class,
                             const Shape& input_shape, uint32_t window_w,
                             uint32_t window_h);

// Max number of events passed to the DMA engine
uint32_t max_dma_events();

void alloc_dma_descriptors(dma::Engine* dma, MemoryMgr* mem_mgr,
                           uint32_t num_descriptors);

}  // namespace dma
}  // namespace akida
