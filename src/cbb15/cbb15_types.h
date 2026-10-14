// BrainBoard1500 - shared types: status codes, model descriptor, slot header.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cbb15 {

enum class Status : uint8_t {
  Ok = 0,
  NotStarted,
  InvalidArgument,
  ExpanderMissing,
  PowerMonitorMissing,
  LinkFailed,
  WrongManufacturerId,
  WrongIpVersion,
  BridgeFailed,
  FlashUnknown,
  FlashQuadEnableFailed,
  FlashEraseFailed,
  FlashProgramFailed,
  FlashVerifyFailed,
  SlotEmpty,
  SlotMismatch,
  ModelTooLarge,
  ModelInvalid,
  ModelNotLoaded,
  DeviceCreateFailed,
  ProgramFailed,
  EnqueueFailed,
  FetchTimeout,
  OutputInvalid,
  LearningUnavailable,
  StoreEmpty,
};

const char* statusName(Status status);

// Descriptor of a model exported by tools/export_model.py.
//
// The serialized Akida program is "program info" (a size-prefixed
// FlatBuffer that always stays on the host) followed by "program data"
// (weights and register settings that the AKD1500 needs on-chip).
//   - Slot 0 (SPI push) needs the complete blob.
//   - Slots 1-4 only need the program info; the program data is read by the
//     AKD1500 itself from the BB15 flash. The exported headers therefore
//     offer two accessors so that sketches that boot from flash do not link
//     the (large) program data into the Nicla flash.
struct Model {
  const char* name = nullptr;
  const uint8_t* info = nullptr;  // program info (prefix of the blob)
  uint32_t infoSize = 0;
  const uint8_t* blob = nullptr;  // complete program, nullptr if not linked
  uint32_t blobSize = 0;
  uint32_t dataSize = 0;     // blobSize - infoSize
  uint32_t dataCrc32 = 0;    // CRC-32 (IEEE) of the program data
  uint32_t infoCrc32 = 0;    // CRC-32 (IEEE) of the program info
  uint16_t inputHeight = 0;  // Akida input tensor is [height][width][channels]
  uint16_t inputWidth = 0;
  uint16_t inputChannels = 0;
  uint16_t outputCount = 0;
  const char* const* labels = nullptr;
  uint16_t labelCount = 0;

  bool hasBlob() const { return blob != nullptr && blobSize > infoSize; }
  uint32_t inputSize() const {
    return static_cast<uint32_t>(inputHeight) * inputWidth * inputChannels;
  }
  const char* label(uint16_t index) const {
    return (labels != nullptr && index < labelCount) ? labels[index] : "?";
  }
};

// Header stored in the first sector of every flash slot.
struct SlotHeader {
  static constexpr uint32_t kMagic = 0x35314243;  // "CB15"
  static constexpr uint16_t kVersion = 1;

  uint32_t magic;
  uint16_t version;
  uint16_t headerSize;
  char name[32];
  uint32_t dataOffset;  // offset of the program data inside the slot
  uint32_t dataSize;
  uint32_t dataCrc32;
  uint32_t infoSize;
  uint32_t infoCrc32;
  uint32_t reserved[4];
  uint32_t headerCrc32;  // CRC-32 of all preceding bytes

  bool valid() const;
  bool matches(const Model& model) const;
};

// Header in front of user data (for example learned weights) in a slot.
struct UserDataHeader {
  static constexpr uint32_t kMagic = 0x4D4C4243;  // "CBLM"

  uint32_t magic;
  uint32_t size;          // payload bytes
  uint32_t payloadCrc32;
  uint32_t modelInfoCrc32;  // binds the payload to one model
  uint32_t headerCrc32;
};

}  // namespace cbb15
