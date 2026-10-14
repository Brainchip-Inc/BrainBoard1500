#include "cbb15_flash.h"

#include <string.h>

#include "cbb15_config.h"

namespace cbb15 {

namespace {

// Standard SPI NOR commands (identical on AT25SL321 and W25Q64JW).
constexpr uint8_t kCmdWriteEnable = 0x06;
constexpr uint8_t kCmdReadStatus1 = 0x05;
constexpr uint8_t kCmdReadData = 0x03;
constexpr uint8_t kCmdPageProgram = 0x02;
constexpr uint8_t kCmdSectorErase = 0x20;
constexpr uint8_t kCmdReadJedec = 0x9F;
constexpr uint8_t kCmdReleasePowerDown = 0xAB;
constexpr uint8_t kCmdResetEnable = 0x66;
constexpr uint8_t kCmdReset = 0x99;
constexpr uint8_t kStatusBusy = 0x01;
constexpr uint8_t kStatusWel = 0x02;

constexpr uint32_t kEraseTimeoutMs = 1000;
constexpr uint32_t kProgramTimeoutMs = 20;
constexpr size_t kReadChunk = 256;

// Profiles. Both chips use the same XIP read: quad output fast read (0x6B),
// instruction and address on one line, 8 dummy clocks, no mode bits.
const FlashProfile kProfiles[] = {
    {FlashChip::RenesasAT25SL321, "Renesas AT25SL321 (4 MiB)", 0x1F, 0x42, 0x16,
     4u * 1024u * 1024u, 0x6B, 0, 8, 0x35, 0x31, 0x02},
    {FlashChip::WinbondW25Q64JW, "Winbond W25Q64JW (8 MiB, BB15 rev 1)", 0xEF,
     0x60, 0x17, 8u * 1024u * 1024u, 0x6B, 0, 8, 0x35, 0x31, 0x02},
    {FlashChip::Unknown, "unknown", 0, 0, 0, 0, 0x6B, 0, 8, 0x35, 0x31, 0x02},
};

// SPI master register fields (Akida Engine api/akd1500/spi_flash.h).
constexpr uint32_t field(uint32_t value, uint8_t lsb) { return value << lsb; }

}  // namespace

const FlashProfile& flashProfile(FlashChip chip) {
  for (const auto& p : kProfiles) {
    if (p.chip == chip) {
      return p;
    }
  }
  return kProfiles[2];
}

void FlashBridge::attach(SpiLink& link, akida::HardwareDriver& driver) {
  link_ = &link;
  driver_ = &driver;
}

void FlashBridge::applyRuntimeProfile(const FlashProfile& profile) {
  // Akida reads big-endian words from the flash: keep the byte swap on the
  // SPI master input, and make sure the S2M pin mux is off.
  uint32_t control = driver_->read32(kRegControlSignals);
  control |= kControlSpimDiSwap;
  control &= ~kControlEnSpiS2m;
  driver_->write32(kRegControlSignals, control);

  // The controller must be disabled while it is configured.
  driver_->write32(kSpimSsienr, 0);
  // Master mode, quad frame format, 32-bit data frames.
  driver_->write32(kSpimCtrlr0,
                   field(1, 31) | field(2, 22) | field(0x1F, 0));
  // Slave select 0 = flash.
  driver_->write32(kSpimSer, 1);
  // SPI clock = 400 MHz / (2 * SCKDV). SCKDV = 4 gives 50 MHz, a safe value
  // for both 1.8 V flash chips (both are rated above 100 MHz).
  driver_->write32(kSpimBaudr, field(4, 1));
  // XIP transfer: TRANS_TYPE, 24-bit address (ADDR_L = 6), 8-bit
  // instruction (INST_L = 2), dummy clocks, instruction phase enabled.
  driver_->write32(kSpimSpiCtrlr0,
                   field(profile.transferType, 0) | field(6, 2) | field(2, 8) |
                       field(profile.waitCycles, 11) | field(1, 20));
  driver_->write32(kSpimDdrDriveEdge, 0);
  driver_->write32(kSpimXipModeBits, 0);
  driver_->write32(kSpimXipIncrInst, profile.readOpcode);
  driver_->write32(kSpimSsienr, 1);
}

Status FlashBridge::probe(FlashChip expected, uint32_t* jedecOut) {
  const Status status = open();
  if (status != Status::Ok) {
    return status;
  }
  resetPreamble();
  const uint32_t jedec = readJedec();
  if (jedecOut != nullptr) {
    *jedecOut = jedec;
  }
  const FlashProfile& profile = flashProfile(expected);
  const uint32_t expectedJedec = (uint32_t(profile.manufacturerId) << 16) |
                                 (uint32_t(profile.memoryType) << 8) |
                                 profile.capacityId;
  if (jedec != expectedJedec) {
    close();
    return Status::FlashUnknown;
  }
  const bool qe = ensureQuadEnable(profile);
  close();
  applyRuntimeProfile(profile);
  return qe ? Status::Ok : Status::FlashQuadEnableFailed;
}

Status FlashBridge::open() {
  if (open_) {
    return Status::Ok;
  }
  if (link_ == nullptr || driver_ == nullptr) {
    return Status::NotStarted;
  }
  savedControl_ = driver_->read32(kRegControlSignals);
  driver_->write32(kRegControlSignals, savedControl_ | kControlEnSpiS2m);
  bool latched = false;
  for (int attempt = 0; attempt < 3 && !latched; ++attempt) {
    latched = (driver_->read32(kRegControlSignals) & kControlEnSpiS2m) != 0;
    if (!latched) {
      delay(1);
    }
  }
  delay(kBridgeSwitchSettleMs);
  if (!latched) {
    return Status::BridgeFailed;
  }
  open_ = true;
  return Status::Ok;
}

void FlashBridge::close() {
  if (!open_) {
    return;
  }
  driver_->write32(kRegControlSignals, savedControl_ & ~kControlEnSpiS2m);
  delay(kBridgeSwitchSettleMs);
  open_ = false;
}

void FlashBridge::command(uint8_t opcode) {
  buffer_[0] = opcode;
  link_->bridgeTransfer(buffer_, 1);
}

void FlashBridge::resetPreamble() {
  // Wake the flash from deep power-down and issue a software reset so that
  // it is in a known state (no pending continuous-read mode).
  command(kCmdReleasePowerDown);
  delayMicroseconds(50);
  command(kCmdResetEnable);
  command(kCmdReset);
  // The reset needs a few tens of microseconds; 20 ms is a generous margin.
  delay(20);
}

uint32_t FlashBridge::readJedec() {
  buffer_[0] = kCmdReadJedec;
  buffer_[1] = buffer_[2] = buffer_[3] = 0;
  link_->bridgeTransfer(buffer_, 4);
  return (uint32_t(buffer_[1]) << 16) | (uint32_t(buffer_[2]) << 8) |
         buffer_[3];
}

uint8_t FlashBridge::readStatus(uint8_t commandCode) {
  buffer_[0] = commandCode;
  buffer_[1] = 0;
  link_->bridgeTransfer(buffer_, 2);
  return buffer_[1];
}

bool FlashBridge::waitReady(uint32_t timeoutMs) {
  const uint32_t start = millis();
  do {
    if ((readStatus(kCmdReadStatus1) & kStatusBusy) == 0) {
      return true;
    }
  } while (millis() - start <= timeoutMs);
  return false;
}

bool FlashBridge::writeEnable() {
  command(kCmdWriteEnable);
  return (readStatus(kCmdReadStatus1) & kStatusWel) != 0;
}

bool FlashBridge::eraseSector(uint32_t address) {
  if (!writeEnable()) {
    return false;
  }
  buffer_[0] = kCmdSectorErase;
  buffer_[1] = static_cast<uint8_t>(address >> 16);
  buffer_[2] = static_cast<uint8_t>(address >> 8);
  buffer_[3] = static_cast<uint8_t>(address);
  link_->bridgeTransfer(buffer_, 4);
  return waitReady(kEraseTimeoutMs);
}

bool FlashBridge::programPage(uint32_t address, const uint8_t* data,
                              size_t size) {
  if (size == 0 || size > kFlashPageSize ||
      (address % kFlashPageSize) + size > kFlashPageSize) {
    return false;
  }
  if (!writeEnable()) {
    return false;
  }
  buffer_[0] = kCmdPageProgram;
  buffer_[1] = static_cast<uint8_t>(address >> 16);
  buffer_[2] = static_cast<uint8_t>(address >> 8);
  buffer_[3] = static_cast<uint8_t>(address);
  memcpy(buffer_ + 4, data, size);
  link_->bridgeTransfer(buffer_, 4 + size);
  return waitReady(kProgramTimeoutMs);
}

bool FlashBridge::read(uint32_t address, uint8_t* data, size_t size) {
  while (size > 0) {
    const size_t n = size < kReadChunk ? size : kReadChunk;
    buffer_[0] = kCmdReadData;
    buffer_[1] = static_cast<uint8_t>(address >> 16);
    buffer_[2] = static_cast<uint8_t>(address >> 8);
    buffer_[3] = static_cast<uint8_t>(address);
    memset(buffer_ + 4, 0, n);
    link_->bridgeTransfer(buffer_, 4 + n);
    memcpy(data, buffer_ + 4, n);
    address += n;
    data += n;
    size -= n;
  }
  return true;
}

bool FlashBridge::write(uint32_t address, const uint8_t* data, size_t size) {
  const uint32_t first = address & ~(kFlashSectorSize - 1);
  const uint32_t last = (address + size + kFlashSectorSize - 1) &
                        ~(kFlashSectorSize - 1);
  for (uint32_t sector = first; sector < last; sector += kFlashSectorSize) {
    if (!eraseSector(sector)) {
      return false;
    }
  }
  while (size > 0) {
    const size_t room = kFlashPageSize - (address % kFlashPageSize);
    const size_t n = size < room ? size : room;
    if (!programPage(address, data, n)) {
      return false;
    }
    address += n;
    data += n;
    size -= n;
  }
  return true;
}

bool FlashBridge::ensureQuadEnable(const FlashProfile& profile) {
  uint8_t status2 = readStatus(profile.qeReadCommand);
  if ((status2 & profile.qeMask) != 0) {
    return true;
  }
  if (!writeEnable()) {
    return false;
  }
  buffer_[0] = profile.qeWriteCommand;
  buffer_[1] = static_cast<uint8_t>(status2 | profile.qeMask);
  link_->bridgeTransfer(buffer_, 2);
  if (!waitReady(kEraseTimeoutMs)) {
    return false;
  }
  status2 = readStatus(profile.qeReadCommand);
  return (status2 & profile.qeMask) != 0;
}

}  // namespace cbb15
