#pragma once

#include <cstdint>

#include "akida/hardware_device.h"
#include "engine/registers_dma_engine.h"

namespace akida {

namespace dma {

struct Engine {
 public:
  explicit Engine(uint32_t reg_base_addr, uint32_t desc_bytes_size,
                  uint32_t alignment);

  dma::addr descriptor_base_addr;
  const uint32_t descriptor_bytes_size;
  const uint32_t reg_base_addr;
  const uint32_t addr_alignment;
};

struct Config {
  Engine engine;
};

struct Input {
  explicit Input(Engine e) : engine(e) {}

  virtual ~Input() = default;
  // BrainBoard1500 patch (RTTI-free type query), see PATCHES.md
  virtual bool is_hrc() const { return false; }
  Engine engine;
};

struct Hrc : public Input {
  explicit Hrc(Engine e) : Input(e) {}
  bool is_hrc() const override { return true; }
};

struct Event : public Input {
  explicit Event(Engine e) : Input(e) {}
};

struct Skip {
  Engine engine;
  dma::addr outputs_base_address{0};
};
}  // namespace dma

}  // namespace akida
