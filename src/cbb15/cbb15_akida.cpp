#include "cbb15_akida.h"

#include <string.h>

#include "akida/dense.h"
#include "akida/input_conversion.h"
#include "akida/sparse.h"
#include "cbb15_crc32.h"

namespace cbb15 {

namespace {

template <typename T>
void appendScores(const akida::Dense& dense, std::vector<float>& out) {
  const T* values = dense.data<T>();
  const size_t n = dense.size();
  out.resize(n);
  for (size_t i = 0; i < n; ++i) {
    out[i] = static_cast<float>(values[i]);
  }
}

bool denseToScores(const akida::Dense& dense, std::vector<float>& out) {
  switch (dense.type()) {
    case akida::TensorType::float32:
      appendScores<float>(dense, out);
      return true;
    case akida::TensorType::int32:
      appendScores<int32_t>(dense, out);
      return true;
    case akida::TensorType::int16:
      appendScores<int16_t>(dense, out);
      return true;
    case akida::TensorType::uint16:
      appendScores<uint16_t>(dense, out);
      return true;
    case akida::TensorType::uint8:
    case akida::TensorType::uint4:
    case akida::TensorType::uint2:
      appendScores<uint8_t>(dense, out);
      return true;
    case akida::TensorType::int8:
    case akida::TensorType::int4:
    case akida::TensorType::int2:
    case akida::TensorType::bit:
      appendScores<int8_t>(dense, out);
      return true;
  }
  return false;
}

void rank(InferenceResult& r) {
  r.best = -1;
  r.bestScore = r.secondScore = -3.4e38f;
  for (size_t i = 0; i < r.scores.size(); ++i) {
    const float v = r.scores[i];
    if (v > r.bestScore) {
      r.secondScore = r.bestScore;
      r.bestScore = v;
      r.best = static_cast<int32_t>(i);
    } else if (v > r.secondScore) {
      r.secondScore = v;
    }
  }
  if (r.scores.size() < 2) {
    r.secondScore = r.bestScore;
  }
}

}  // namespace

Status Akida::begin(Board& board) {
  if (!board.started() || board.driver() == nullptr) {
    return Status::NotStarted;
  }
  board_ = &board;
  device_ = akida::HardwareDevice::create(board.driver());
  loaded_ = false;
  learning_ = false;
  pending_ = 0;
  streaming_ = false;
  streamDepth_ = 0;
  return device_ ? Status::Ok : Status::DeviceCreateFailed;
}

void Akida::end() {
  // Call before Board::powerOff(): the device uses the board's SPI driver.
  device_.reset();
  loaded_ = false;
  learning_ = false;
  pending_ = 0;
  streaming_ = false;
  streamDepth_ = 0;
}

Status Akida::load(const Model& model, uint8_t slot, bool checkSlotHeader,
                   Print* log) {
  if (!device_) {
    return Status::NotStarted;
  }
  if (slot > kSlotCount || model.info == nullptr || model.infoSize < 8) {
    return Status::InvalidArgument;
  }
  // The program info is a size-prefixed FlatBuffer: 4-byte little-endian
  // length, then the table. The exporter stores the full prefix length.
  const uint32_t prefix = uint32_t(model.info[0]) | (uint32_t(model.info[1]) << 8) |
                          (uint32_t(model.info[2]) << 16) |
                          (uint32_t(model.info[3]) << 24);
  if (prefix + 4 != model.infoSize ||
      crc32(model.info, model.infoSize) != model.infoCrc32) {
    return Status::ModelInvalid;
  }
  if (slot == 0 && !model.hasBlob()) {
    return Status::InvalidArgument;
  }
  if (slot > 0 && checkSlotHeader) {
    const Status st = board_->checkSlot(slot, model);
    if (log != nullptr && st != Status::Ok) {
      log->print("slot check: ");
      log->println(statusName(st));
    }
    if (st != Status::Ok) {
      return st;
    }
  }
  model_ = model;
  slot_ = slot;
  return program();
}

Status Akida::program() {
  if (!drain()) {
    return Status::FetchTimeout;
  }
  loaded_ = false;
  learning_ = false;
  // Reprogramming resets the AKD1500 memory, including a stream buffer.
  streaming_ = false;
  streamDepth_ = 0;
  const uint32_t t0 = millis();
  if (slot_ == 0) {
    info_ = device_->program(model_.blob, model_.blobSize);
  } else {
    // The AKD1500 fetches the program data from flash during programming.
    board_->applyFlashRuntimeProfile();
    const uint32_t address =
        kFlashXipBase + slotFlashOffset(slot_) + kSlotDataOffset;
    info_ = device_->program_external_data(model_.info, model_.infoSize,
                                           address);
    // Programming resets parts of the core: restore the XIP configuration.
    board_->applyFlashRuntimeProfile();
  }
  if (!info_.is_valid()) {
    return Status::ProgramFailed;
  }
  // One input in flight at a time; the input buffer lives in AKD1500 memory
  // because the AKD1500 cannot read the host memory.
  device_->set_batch_size(1, true);
  // Core clock counter of the input DMA: measures the pure inference time.
  device_->toggle_clock_counter(true);
  // Classifier heads without activation return potentials that need the
  // per-channel scale (dequantize). Learning heads are compared raw.
  dequantize_ = !info_.activation_enabled() && !info_.can_learn();
  loadTimeMs_ = millis() - t0;
  loaded_ = true;
  pending_ = 0;
  return Status::Ok;
}

bool Akida::drain() {
  // A job that timed out is still in the AKD1500 pipeline. It has to be
  // fetched before anything is reconfigured, otherwise the engine refuses
  // to change the batch size.
  const uint32_t start = millis();
  while (pending_ > 0) {
    if (device_->fetch()) {
      --pending_;
      continue;
    }
    if (millis() - start > fetchTimeoutMs) {
      return false;
    }
    delayMicroseconds(100);
  }
  return true;
}

Status Akida::infer(const uint8_t* input, InferenceResult& result) {
  return run(input, nullptr, &result);
}

Status Akida::run(const uint8_t* input, const int32_t* label,
                  InferenceResult* result) {
  if (!loaded_) {
    return Status::ModelNotLoaded;
  }
  if (input == nullptr || streaming_) {
    return Status::InvalidArgument;  // stopStream() first
  }
  if (!drain()) {
    return Status::FetchTimeout;
  }
  if (slot_ > 0) {
    // Cheap (a few register writes): keeps the AKD1500 flash reads valid.
    board_->applyFlashRuntimeProfile();
  }
  const akida::Shape dims = info_.input_dims();
  auto dense = akida::Dense::create_view(reinterpret_cast<const char*>(input),
                                         akida::TensorType::uint8, dims,
                                         akida::Dense::Layout::RowMajor);
  akida::TensorUniquePtr sparse;
  const akida::Tensor* tensor = dense.get();
  if (!info_.input_is_dense()) {
    sparse = akida::conversion::to_sparse(*dense, info_);
    tensor = sparse.get();
  }

  // Restart the core clock counter for this job (disable + enable is what the
  // engine itself does to reset it) and keep the start value, so the result
  // is correct whether the counter restarts or keeps accumulating.
  device_->toggle_clock_counter(false);
  device_->toggle_clock_counter(true);
  const uint32_t c0 = device_->read_clock_counter();

  const uint32_t t0 = micros();
  if (!device_->enqueue(*tensor, label)) {
    return Status::EnqueueFailed;
  }
  ++pending_;
  akida::TensorUniquePtr output;
  const uint32_t start = millis();
  while (!(output = device_->fetch())) {
    if (millis() - start > fetchTimeoutMs) {
      ++timeouts_;
      return Status::FetchTimeout;
    }
  }
  --pending_;
  const uint32_t latency = micros() - t0;
  const uint32_t c1 = device_->read_clock_counter();
  if (result == nullptr) {
    return Status::Ok;
  }
  result->computeCycles = c1 >= c0 ? c1 - c0 : c1;
  result->computeUs = static_cast<uint32_t>(
      static_cast<uint64_t>(result->computeCycles) * 1000000ull / coreClockHz);
  result->latencyUs = latency;
  return toResult(*output, *result);
}

Status Akida::toResult(const akida::Tensor& output, InferenceResult& result) {
  const akida::Dense* out = akida::conversion::as_dense(output);
  akida::DenseUniquePtr converted;
  if (out == nullptr) {
    const akida::Sparse* s = akida::conversion::as_sparse(output);
    if (s == nullptr) {
      return Status::OutputInvalid;
    }
    converted = akida::conversion::to_dense(*s);
    out = converted.get();
  }
  akida::DenseUniquePtr dequantized;
  if (dequantize_) {
    dequantized = device_->dequantize(*out);
    out = dequantized.get();
  }
  if (out == nullptr || !denseToScores(*out, result.scores)) {
    return Status::OutputInvalid;
  }
  rank(result);
  return Status::Ok;
}

uint32_t Akida::inputBytes() const {
  uint32_t n = 1;
  for (const auto d : info_.input_dims()) {
    n *= d;
  }
  return n;
}

Status Akida::startStream(const uint8_t* input, uint8_t depth) {
  if (!loaded_) {
    return Status::ModelNotLoaded;
  }
  if (input == nullptr || depth == 0 || !info_.input_is_dense() || learning_) {
    return Status::InvalidArgument;
  }
  if (streaming_) {
    const Status st = stopStream();
    if (st != Status::Ok) {
      return st;
    }
  }
  if (!drain()) {
    return Status::FetchTimeout;
  }
  // Pipeline of `depth` jobs; inputs are not allocated per job because all
  // jobs read the same buffer that is reserved below.
  streamDepth_ = static_cast<uint8_t>(device_->set_batch_size(depth, false));
  streamInput_ = device_->scratch_alloc(inputBytes());
  streaming_ = true;
  lastStepUs_ = 0;
  streamIdleUs_ = 0;
  return updateStreamInput(input);
}

Status Akida::updateStreamInput(const uint8_t* input) {
  if (!streaming_ || input == nullptr) {
    return Status::InvalidArgument;
  }
  if (slot_ > 0) {
    board_->applyFlashRuntimeProfile();
  }
  board_->driver()->write(streamInput_, input, inputBytes());
  return Status::Ok;
}

Status Akida::streamStep(uint32_t& completed, InferenceResult* latest) {
  completed = 0;
  if (!streaming_) {
    return Status::InvalidArgument;
  }
  // Collect everything that finished, keep only the newest output.
  const uint32_t now = micros();
  const uint32_t queuedBefore = pending_;
  akida::TensorUniquePtr newest;
  while (pending_ > 0) {
    akida::TensorUniquePtr out = device_->fetch();
    if (!out) {
      break;
    }
    --pending_;
    ++completed;
    newest = std::move(out);
  }
  // Every queued job finished: the AKD1500 may have been idle for (at most)
  // the whole time since the previous step.
  if (lastStepUs_ != 0 && (queuedBefore == 0 || pending_ == 0)) {
    streamIdleUs_ += now - lastStepUs_;
  }
  // Refill: enqueue() returns false as soon as the pipeline is full.
  while (pending_ < streamDepth_ && device_->enqueue(streamInput_)) {
    ++pending_;
  }
  lastStepUs_ = micros();
  if (latest != nullptr && newest) {
    return toResult(*newest, *latest);
  }
  return Status::Ok;
}

Status Akida::streamOne(uint32_t* busyUs) {
  if (!streaming_) {
    return Status::InvalidArgument;
  }
  // Collect whatever is still in flight first, so exactly one job runs.
  if (!drain()) {
    return Status::FetchTimeout;
  }
  const uint32_t t0 = micros();
  if (!device_->enqueue(streamInput_)) {
    return Status::EnqueueFailed;
  }
  ++pending_;
  const uint32_t start = millis();
  while (!device_->fetch()) {
    if (millis() - start > fetchTimeoutMs) {
      ++timeouts_;
      return Status::FetchTimeout;
    }
  }
  --pending_;
  if (busyUs != nullptr) {
    *busyUs = micros() - t0;
  }
  return Status::Ok;
}

uint64_t Akida::takeStreamIdleUs() {
  const uint64_t idle = streamIdleUs_;
  streamIdleUs_ = 0;
  return idle;
}

Status Akida::stopStream() {
  if (!streaming_) {
    return Status::Ok;
  }
  if (!drain()) {
    return Status::FetchTimeout;
  }
  device_->scratch_free(streamInput_);
  streaming_ = false;
  streamDepth_ = 0;
  // Back to one job at a time with an engine-managed input buffer.
  device_->set_batch_size(1, true);
  return Status::Ok;
}

bool Akida::canLearn() const { return loaded_ && info_.can_learn(); }

Status Akida::setLearning(bool enabled) {
  if (!canLearn()) {
    return Status::LearningUnavailable;
  }
  if (streaming_) {
    return Status::InvalidArgument;  // stopStream() first
  }
  if (!drain()) {
    return Status::FetchTimeout;
  }
  if (enabled != learning_) {
    device_->toggle_learn(enabled);
    learning_ = enabled;
  }
  return Status::Ok;
}

Status Akida::learn(const uint8_t* input, int32_t label,
                    InferenceResult* result) {
  if (!canLearn()) {
    return Status::LearningUnavailable;
  }
  // Learning mode is only switched on for this labelled step. With learning
  // enabled the engine would also use unlabelled inputs (learn class 0), so
  // plain inference always runs with learning off.
  const bool wasLearning = learning_;
  Status st = setLearning(true);
  if (st != Status::Ok) {
    return st;
  }
  InferenceResult scratch;
  st = run(input, &label, result != nullptr ? result : &scratch);
  if (!wasLearning) {
    const Status off = setLearning(false);
    if (st == Status::Ok) {
      st = off;
    }
  }
  return st;
}

size_t Akida::learnMemoryWords() const {
  return canLearn() ? device_->learn_mem_size() : 0;
}

Status Akida::readLearnMemory(std::vector<uint32_t>& words) {
  if (!canLearn()) {
    return Status::LearningUnavailable;
  }
  // The engine only exposes the learn memory while learning is enabled.
  const bool wasLearning = learning_;
  Status st = setLearning(true);
  if (st != Status::Ok) {
    return st;
  }
  words.resize(device_->learn_mem_size());
  device_->learn_mem(words.data());
  return wasLearning ? Status::Ok : setLearning(false);
}

Status Akida::writeLearnMemory(const std::vector<uint32_t>& words) {
  if (!canLearn()) {
    return Status::LearningUnavailable;
  }
  if (words.size() != device_->learn_mem_size()) {
    return Status::InvalidArgument;
  }
  if (!drain()) {
    return Status::FetchTimeout;
  }
  device_->update_learn_mem(words.data());
  return Status::Ok;
}

Status Akida::saveLearnMemory(uint8_t storeSlot) {
  std::vector<uint32_t> words;
  const Status st = readLearnMemory(words);
  if (st != Status::Ok) {
    return st;
  }
  return board_->writeUserData(storeSlot, words.data(),
                               words.size() * sizeof(uint32_t),
                               model_.infoCrc32);
}

Status Akida::restoreLearnMemory(uint8_t storeSlot) {
  if (!canLearn()) {
    return Status::LearningUnavailable;
  }
  std::vector<uint32_t> words(device_->learn_mem_size());
  uint32_t size = 0;
  const Status st =
      board_->readUserData(storeSlot, words.data(),
                           words.size() * sizeof(uint32_t), size,
                           model_.infoCrc32);
  if (st != Status::Ok) {
    return st;
  }
  if (size != words.size() * sizeof(uint32_t)) {
    return Status::StoreEmpty;
  }
  return writeLearnMemory(words);
}

Status Akida::forget() {
  if (!loaded_) {
    return Status::ModelNotLoaded;
  }
  return program();
}

}  // namespace cbb15
