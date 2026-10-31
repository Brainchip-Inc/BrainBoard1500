// BrainBoard1500 - Akida Engine 2.19.3 runtime on the BB15.
//
// Model loading:
//   slot 0     the complete program is pushed over SPI into the AKD1500
//              (HardwareDevice::program)
//   slot 1..4  only the program info is sent; the AKD1500 reads the program
//              data itself from the BB15 flash through its SPI master
//              (HardwareDevice::program_external_data at 0x8000_0000 + slot)
//              A program that would read the flash during every job (more
//              than one pass, or FNP2 weights) is instead copied from its
//              slot into the top of the AKD1500 SRAM when it fits there, and
//              runs from that copy (program_external_data at the copy).
//
// Inference uses one input at a time (batch size 1). Outputs are converted
// to float scores: dequantized potentials if the last layer has no
// activation, otherwise the activations themselves.
#pragma once

#include <Arduino.h>

#include <memory>
#include <vector>

#include "akida/hardware_device.h"
#include "akida/program_info.h"
#include "cbb15_board.h"
#include "cbb15_types.h"

namespace cbb15 {

struct InferenceResult {
  std::vector<float> scores;
  int32_t best = -1;
  float bestScore = 0.0f;
  float secondScore = 0.0f;
  uint32_t latencyUs = 0;  // enqueue (input transfer) until output fetched
  // Pure inference time measured by the AKD1500 itself: core clock cycles
  // counted by its input DMA buffer timer while the job is processed. The SPI
  // transfer of the input and the output fetch are not included.
  uint32_t computeCycles = 0;
  uint32_t computeUs = 0;

  float margin() const { return bestScore - secondScore; }
};

// The Akida Engine allocates its buffers in the AKD1500 SRAM window its driver
// reports, and reads that window once, when a device is created. This driver
// passes everything through to the board's driver but leaves the top of the
// SRAM out of the window, so program data copied there is never allocated
// over.
class SramWindow final : public akida::HardwareDriver {
 public:
  void attach(akida::HardwareDriver* driver) { driver_ = driver; }
  // Keeps the top `bytes` of the SRAM from devices created afterwards.
  void reserveTop(uint32_t bytes) { reserved_ = bytes; }
  uint32_t reserved() const { return reserved_; }

  const char* desc() const override { return driver_->desc(); }
  uint32_t scratch_memory() const override { return driver_->scratch_memory(); }
  uint32_t scratch_size() const override {
    return driver_->scratch_size() - reserved_;
  }
  uint32_t top_level_reg() const override { return driver_->top_level_reg(); }
  uint32_t akida_visible_memory() const override {
    return driver_->akida_visible_memory();
  }
  uint32_t akida_visible_memory_size() const override {
    return driver_->akida_visible_memory_size();
  }
  void read(uint32_t address, void* data, size_t size) const override {
    driver_->read(address, data, size);
  }
  void write(uint32_t address, const void* data, size_t size) override {
    driver_->write(address, data, size);
  }

 private:
  akida::HardwareDriver* driver_ = nullptr;
  uint32_t reserved_ = 0;
};

class Akida {
 public:
  Status begin(Board& board);
  void end();
  bool started() const { return device_ != nullptr; }

  // Loads a model from slot 0 (SPI push) or a flash slot 1..4. When
  // checkSlotHeader is set, the flash slot header is read first (about
  // 0.6 s through the S2M bridge) to make sure the slot holds this model.
  Status load(const Model& model, uint8_t slot, bool checkSlotHeader = false,
              Print* log = nullptr);
  bool loaded() const { return loaded_; }
  const Model& model() const { return model_; }
  uint8_t slot() const { return slot_; }
  // True when the program data of the flash slot was copied into the AKD1500
  // SRAM at load, so jobs never read the flash.
  bool slotInSram() const { return loaded_ && window_.reserved() > 0; }
  uint32_t loadTimeMs() const { return loadTimeMs_; }

  // Runs one inference. The input is uint8 [height][width][channels].
  Status infer(const uint8_t* input, InferenceResult& result);

  // --- On-chip learning (models exported with a learning layer) ----------
  bool canLearn() const;
  Status setLearning(bool enabled);
  bool learning() const { return learning_; }
  // One supervised learning step with the given class label. Learning mode
  // is switched on for this step only; infer() never changes the weights.
  Status learn(const uint8_t* input, int32_t label,
               InferenceResult* result = nullptr);
  // Learned weights: read, restore, and store in a flash slot user area.
  size_t learnMemoryWords() const;
  Status readLearnMemory(std::vector<uint32_t>& words);
  Status writeLearnMemory(const std::vector<uint32_t>& words);
  Status saveLearnMemory(uint8_t storeSlot);
  Status restoreLearnMemory(uint8_t storeSlot);
  // Reprograms the model, which discards everything learned.
  Status forget();

  // --- Streaming: keep the AKD1500 busy without input transfers ----------
  // startStream() writes one input into the AKD1500 SRAM once and enables
  // the hardware pipeline (up to `depth` jobs in flight, max 15 for a
  // single-pass program). streamStep() then refills the pipeline by
  // enqueueing that SRAM address - no image data crosses the SPI bus - and
  // fetches every finished job. Only dense (image/MFCC) inputs.
  Status startStream(const uint8_t* input, uint8_t depth = 15);
  // Replaces the input in AKD1500 SRAM (one SPI transfer), e.g. a new
  // camera frame. Jobs already queued keep using the old data.
  Status updateStreamInput(const uint8_t* input);
  // Returns the number of jobs completed in this call. If `latest` is not
  // null and at least one job completed, it receives the newest output.
  Status streamStep(uint32_t& completed, InferenceResult* latest = nullptr);
  Status stopStream();
  // Exactly one job from the stream input: enqueue, wait for the result.
  // busyUs receives the time from enqueue until the output was available,
  // i.e. the time the AKD1500 had work queued. For paced duty cycles.
  Status streamOne(uint32_t* busyUs = nullptr);
  // Time in which the pipeline ran empty since the last call (upper bound:
  // when a step finds every queued job finished, the whole interval since
  // the previous step is counted as idle). Busy share = 1 - idle / elapsed.
  uint64_t takeStreamIdleUs();
  bool streaming() const { return streaming_; }
  uint8_t streamDepth() const { return streamDepth_; }

  akida::HardwareDevice* device() { return device_.get(); }
  const akida::ProgramInfo& programInfo() const { return info_; }
  uint32_t timeouts() const { return timeouts_; }
  uint32_t fetchTimeoutMs = 2000;
  // AKD1500 core clock in normal mode (PLL from the 25 MHz reference).
  uint32_t coreClockHz = 400000000;

 private:
  Status run(const uint8_t* input, const int32_t* label,
             InferenceResult* result);
  bool drain();
  Status program();
  // Copies the slot's program data into the top of the AKD1500 SRAM if the
  // program reads it during every job and fits there. Returns the bytes the
  // copy takes, or 0 when the program has to run from flash.
  uint32_t copySlotToSram();
  // Bytes the slot's program data takes at the top of the SRAM, or 0 when
  // the program gains nothing from SRAM or does not fit there.
  uint32_t sramBytesNeeded() const;
  // AKD1500 address of program data that takes the top `bytes` of the SRAM.
  uint32_t sramAddress(uint32_t bytes) const;
  // Creates a device whose SRAM window leaves out the top `bytes`, unless
  // the current device already has that window.
  void reserveSram(uint32_t bytes);
  Status toResult(const akida::Tensor& output, InferenceResult& result);
  uint32_t inputBytes() const;

  Board* board_ = nullptr;
  SramWindow window_;
  akida::HardwareDevicePtr device_;
  akida::ProgramInfo info_;
  Model model_;
  uint8_t slot_ = 0;
  bool loaded_ = false;
  bool learning_ = false;
  bool dequantize_ = false;
  uint32_t pending_ = 0;
  uint32_t timeouts_ = 0;
  uint32_t loadTimeMs_ = 0;
  bool streaming_ = false;
  uint8_t streamDepth_ = 0;
  uint32_t streamInput_ = 0;  // AKD1500 address of the stream input
  uint32_t lastStepUs_ = 0;
  uint64_t streamIdleUs_ = 0;
};

}  // namespace cbb15
