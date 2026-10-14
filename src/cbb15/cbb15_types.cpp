#include "cbb15_types.h"

#include <string.h>

#include "cbb15_crc32.h"

namespace cbb15 {

const char* statusName(Status status) {
  switch (status) {
    case Status::Ok: return "ok";
    case Status::NotStarted: return "board not started";
    case Status::InvalidArgument: return "invalid argument";
    case Status::ExpanderMissing: return "I/O expander 0x43 not found";
    case Status::PowerMonitorMissing: return "INA2227 0x40 not found";
    case Status::LinkFailed: return "SPI link failed";
    case Status::WrongManufacturerId: return "wrong AKD1500 manufacturer id";
    case Status::WrongIpVersion: return "wrong AKD1500 IP version";
    case Status::BridgeFailed: return "S2M flash bridge failed";
    case Status::FlashUnknown: return "unknown flash JEDEC id";
    case Status::FlashQuadEnableFailed: return "flash quad enable failed";
    case Status::FlashEraseFailed: return "flash erase failed";
    case Status::FlashProgramFailed: return "flash program failed";
    case Status::FlashVerifyFailed: return "flash verify failed";
    case Status::SlotEmpty: return "flash slot is empty";
    case Status::SlotMismatch: return "flash slot holds another model";
    case Status::ModelTooLarge: return "model does not fit";
    case Status::ModelInvalid: return "model invalid";
    case Status::ModelNotLoaded: return "no model loaded";
    case Status::DeviceCreateFailed: return "Akida device creation failed";
    case Status::ProgramFailed: return "programming the AKD1500 failed";
    case Status::EnqueueFailed: return "enqueue failed";
    case Status::FetchTimeout: return "inference timeout";
    case Status::OutputInvalid: return "unexpected output format";
    case Status::LearningUnavailable: return "model cannot learn";
    case Status::StoreEmpty: return "no stored data";
  }
  return "unknown";
}

bool SlotHeader::valid() const {
  if (magic != kMagic || version != kVersion ||
      headerSize != sizeof(SlotHeader)) {
    return false;
  }
  const uint32_t crc =
      crc32(reinterpret_cast<const uint8_t*>(this), offsetof(SlotHeader, headerCrc32));
  return crc == headerCrc32;
}

bool SlotHeader::matches(const Model& model) const {
  return valid() && dataSize == model.dataSize &&
         dataCrc32 == model.dataCrc32 && infoSize == model.infoSize &&
         infoCrc32 == model.infoCrc32;
}

}  // namespace cbb15
