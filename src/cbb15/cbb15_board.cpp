#include "cbb15_board.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "akd1500/akd1500_spi_driver.h"
#include "akida/registers_top_level.h"
#include "cbb15_crc32.h"

namespace cbb15 {

namespace {

constexpr uint8_t pinBit(uint8_t pin) { return static_cast<uint8_t>(1u << pin); }

// Expander output levels while the AKD1500 is held in reset:
// SPI_MODE_SEL low (single SPI), INA_EN high, AKD_SLEEP low, AKD_RST low.
constexpr uint8_t kLevelsReset = pinBit(expander_pin::kPowerMonitorEnable);

void logLine(Print* log, const char* format, ...)
    __attribute__((format(printf, 2, 3)));

void logLine(Print* log, const char* format, ...) {
  if (log == nullptr) {
    return;
  }
  char line[112];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  log->println(line);
}

}  // namespace

Board::Board() = default;
Board::~Board() = default;

Status Board::begin(const BoardConfig& config, Print* log) {
  config_ = config;
  started_ = false;
  driver_.reset();

  // 1. Power sequencer on (BB15 A8 "RESET" high).
  pinMode(config_.pins.boardPower, OUTPUT);
  digitalWrite(config_.pins.boardPower, HIGH);
  delay(kPowerUpSettleMs);

  // 2. Expander with the AKD1500 held in reset.
  config_.wire->begin();
  config_.wire->setClock(kI2cClockHz);
  expander_.attach(*config_.wire, config_.expanderAddress);
  if (!expander_.present()) {
    logLine(log, "expander 0x%02X missing", config_.expanderAddress);
    return Status::ExpanderMissing;
  }
  if (!expander_.configureBb15(kLevelsReset)) {
    return Status::ExpanderMissing;
  }

  // 3. Chip selects idle high, SPI bus up.
  link_.begin(*config_.spi, config_.pins.akidaCs, config_.pins.bridgeCs,
              config_.akidaSpiClockHz, config_.bridgeSpiClockHz);
  logLine(log, "%s", link_.describe());

  // 4. Reset pulse.
  delay(kResetAssertMs);
  if (!expander_.setOutput(expander_pin::kAkidaReset, true)) {
    return Status::ExpanderMissing;
  }
  delay(kResetReleaseSettleMs);

  // 5. + 6. SPI link test and engine driver.
  const Status link = checkLink(log);
  if (link != Status::Ok) {
    return link;
  }

  // 7. Power monitor (not fatal: the board works without it).
  if (power_.begin(*config_.wire, config_.powerMonitorAddress)) {
    logLine(log, "INA2227 ok (0x%04X)", power_.deviceId());
  } else {
    logLine(log, "INA2227 not found at 0x%02X",
            config_.powerMonitorAddress);
  }

  flash_.attach(link_, *driver_);
  started_ = true;
  if (config_.assumeFlashReady) {
    applyFlashRuntimeProfile();
    logLine(log, "flash: %s, probe skipped", currentFlashProfile().name);
  } else {
    uint32_t jedec = 0;
    const Status st = probeFlash(&jedec);
    logLine(log, "flash probe %s, JEDEC %06lX", statusName(st),
            static_cast<unsigned long>(jedec));
    if (st != Status::Ok) {
      return st;
    }
  }
  return Status::Ok;
}

Status Board::checkLink(Print* log) {
  // Manufacturer id first, with a plain SPI read before the engine driver
  // touches the chip. This is the SPI connection test.
  uint32_t ip = 0;
  const uint8_t vendor = readManufacturerId(&ip);
  logLine(log, "AKD1500 ID 0x%02X %s", vendor,
          vendor == kBrainChipVendorId ? "BrainChip" : "unexpected");
  if (vendor != kBrainChipVendorId) {
    return (ip == 0 || ip == 0xFFFFFFFF) ? Status::LinkFailed
                                         : Status::WrongManufacturerId;
  }

  // The engine driver configures the SPI master for XIP in its constructor.
  driver_.reset(new akida::Akd1500SpiDriver(&link_, 0, 0));
  for (int round = 0; round < 2; ++round) {
    ip = readIpVersion();
    if (ip != kExpectedIpVersion) {
      logLine(log, "IP 0x%08lX, expected 0x%08lX",
              static_cast<unsigned long>(ip),
              static_cast<unsigned long>(kExpectedIpVersion));
      return Status::WrongIpVersion;
    }
    delay(kLinkSettleMs);
  }
  logLine(log, "IP 0x%08lX rev %u, SPI %lu MHz",
          static_cast<unsigned long>(ip), chipRevision(),
          static_cast<unsigned long>(config_.akidaSpiClockHz / 1000000));
  return Status::Ok;
}

uint32_t Board::rawRead32(uint32_t address) {
  // AKD1500 SPI slave read (datasheet 3.7.4): instruction 0x60 (one word),
  // 24-bit word address = (address - 0xFC000000) / 4, eight wait cycles,
  // then the word MSB first.
  const uint32_t word = (address >> 2) & 0x00FFFFFF;
  uint8_t frame[9] = {0x60, static_cast<uint8_t>(word >> 16),
                      static_cast<uint8_t>(word >> 8),
                      static_cast<uint8_t>(word), 0, 0, 0, 0, 0};
  link_.chip_select(0, true);
  link_.write(frame, 5);
  link_.read(frame + 5, 4);
  link_.chip_select(0, false);
  return (uint32_t(frame[5]) << 24) | (uint32_t(frame[6]) << 16) |
         (uint32_t(frame[7]) << 8) | frame[8];
}

uint8_t Board::readManufacturerId(uint32_t* ipVersion) {
  const uint32_t ip =
      rawRead32(akida::soc::akd1500::kTopLevelRegBase + akida::REG_IP_VERSION);
  if (ipVersion != nullptr) {
    *ipVersion = ip;
  }
  return static_cast<uint8_t>(ip >> 24);
}

uint32_t Board::readIpVersion() {
  if (!driver_) {
    return rawRead32(akida::soc::akd1500::kTopLevelRegBase +
                     akida::REG_IP_VERSION);
  }
  return driver_->read32(driver_->top_level_reg() + akida::REG_IP_VERSION);
}

uint8_t Board::chipRevision() {
  return static_cast<uint8_t>(rawRead32(kRegChipInfo) & 0x0F);
}

void Board::powerOff() {
  if (flash_.isOpen()) {
    flash_.close();
  }
  // Reset asserted, single SPI, awake: the strap is valid the moment the
  // expander port supply returns at the next power-up.
  expander_.writeRegister(Expander::kRegOutput, kLevelsReset);
  link_.end();
  driver_.reset();
  started_ = false;
  digitalWrite(config_.pins.boardPower, LOW);
}

bool Board::sleep() {
  const bool ok = expander_.setOutput(expander_pin::kAkidaSleep, true);
  delay(kSleepEnterSettleMs);
  return ok;
}

bool Board::wake() {
  const bool ok = expander_.setOutput(expander_pin::kAkidaSleep, false);
  delay(kSleepExitSettleMs);
  return ok;
}

akida::HardwareDriver* Board::driver() { return driver_.get(); }

// --- Flash ------------------------------------------------------------------

Status Board::probeFlash(uint32_t* jedec) {
  if (!driver_) {
    return Status::NotStarted;
  }
  return flash_.probe(config_.flashChip, jedec);
}

void Board::applyFlashRuntimeProfile() {
  if (driver_) {
    flash_.applyRuntimeProfile(currentFlashProfile());
  }
}

Status Board::openBridge() {
  if (!driver_) {
    return Status::NotStarted;
  }
  const Status st = flash_.open();
  if (st != Status::Ok) {
    return st;
  }
  // Make sure the flash is awake and in its default mode.
  const FlashProfile& profile = currentFlashProfile();
  const uint32_t expected = (uint32_t(profile.manufacturerId) << 16) |
                            (uint32_t(profile.memoryType) << 8) |
                            profile.capacityId;
  if (flash_.readJedec() != expected) {
    flash_.close();
    applyFlashRuntimeProfile();
    return Status::FlashUnknown;
  }
  return Status::Ok;
}

void Board::closeBridge() {
  flash_.close();
  applyFlashRuntimeProfile();
}

Status Board::readSlotHeader(uint8_t slot, SlotHeader& header) {
  if (slot < 1 || slot > kSlotCount) {
    return Status::InvalidArgument;
  }
  const Status st = openBridge();
  if (st != Status::Ok) {
    return st;
  }
  flash_.read(slotFlashOffset(slot) + kSlotHeaderOffset,
              reinterpret_cast<uint8_t*>(&header), sizeof(header));
  closeBridge();
  return header.valid() ? Status::Ok : Status::SlotEmpty;
}

Status Board::checkSlot(uint8_t slot, const Model& model) {
  SlotHeader header;
  const Status st = readSlotHeader(slot, header);
  if (st != Status::Ok) {
    return st;
  }
  return header.matches(model) ? Status::Ok : Status::SlotMismatch;
}

Status Board::writeModelToSlot(uint8_t slot, const Model& model, Print* log,
                               bool force) {
  if (slot < 1 || slot > kSlotCount || !model.hasBlob()) {
    return Status::InvalidArgument;
  }
  if (model.dataSize > kSlotMaxDataSize) {
    return Status::ModelTooLarge;
  }
  const FlashProfile& profile = currentFlashProfile();
  if (slotFlashOffset(slot) + kSlotSize > profile.capacityBytes) {
    return Status::ModelTooLarge;
  }
  // Integrity of the data compiled into the sketch.
  if (crc32(model.blob + model.infoSize, model.dataSize) != model.dataCrc32 ||
      crc32(model.blob, model.infoSize) != model.infoCrc32) {
    return Status::ModelInvalid;
  }

  Status st = openBridge();
  if (st != Status::Ok) {
    return st;
  }
  if (!flash_.ensureQuadEnable(profile)) {
    closeBridge();
    return Status::FlashQuadEnableFailed;
  }

  const uint32_t base = slotFlashOffset(slot);
  SlotHeader header;
  flash_.read(base + kSlotHeaderOffset, reinterpret_cast<uint8_t*>(&header),
              sizeof(header));
  if (!force && header.matches(model)) {
    logLine(log, "slot %u already holds '%s', write skipped", slot, model.name);
    closeBridge();
    return Status::Ok;
  }

  // Invalidate the old header first so that an interrupted write can never
  // leave a valid-looking slot behind.
  logLine(log, "slot %u: writing '%s' (%lu bytes) ...", slot, model.name,
          static_cast<unsigned long>(model.dataSize));
  const uint32_t t0 = millis();
  if (!flash_.eraseSector(base + kSlotHeaderOffset)) {
    closeBridge();
    return Status::FlashEraseFailed;
  }
  const uint8_t* data = model.blob + model.infoSize;
  if (!flash_.write(base + kSlotDataOffset, data, model.dataSize)) {
    closeBridge();
    return Status::FlashProgramFailed;
  }
  logLine(log, "slot %u: written in %lu ms, verifying ...", slot,
          static_cast<unsigned long>(millis() - t0));

  // Read back and compare byte by byte.
  std::vector<uint8_t> chunk(1024);
  uint32_t crc = 0;
  for (uint32_t offset = 0; offset < model.dataSize; offset += chunk.size()) {
    const uint32_t n = std::min<uint32_t>(chunk.size(), model.dataSize - offset);
    flash_.read(base + kSlotDataOffset + offset, chunk.data(), n);
    if (memcmp(chunk.data(), data + offset, n) != 0) {
      logLine(log, "slot %u: verify mismatch at +0x%05lX", slot,
              static_cast<unsigned long>(offset));
      closeBridge();
      return Status::FlashVerifyFailed;
    }
    crc = crc32Update(crc, chunk.data(), n);
  }
  if (crc != model.dataCrc32) {
    closeBridge();
    return Status::FlashVerifyFailed;
  }

  // Header last: a valid header guarantees verified data.
  memset(&header, 0, sizeof(header));
  header.magic = SlotHeader::kMagic;
  header.version = SlotHeader::kVersion;
  header.headerSize = sizeof(SlotHeader);
  strncpy(header.name, model.name, sizeof(header.name) - 1);
  header.dataOffset = kSlotDataOffset;
  header.dataSize = model.dataSize;
  header.dataCrc32 = model.dataCrc32;
  header.infoSize = model.infoSize;
  header.infoCrc32 = model.infoCrc32;
  header.headerCrc32 = crc32(reinterpret_cast<const uint8_t*>(&header),
                             offsetof(SlotHeader, headerCrc32));
  if (!flash_.programPage(base + kSlotHeaderOffset,
                          reinterpret_cast<const uint8_t*>(&header),
                          sizeof(header))) {
    closeBridge();
    return Status::FlashProgramFailed;
  }
  logLine(log, "slot %u: '%s' verified (total %lu ms)", slot, model.name,
          static_cast<unsigned long>(millis() - t0));
  closeBridge();
  return Status::Ok;
}

Status Board::eraseSlot(uint8_t slot) {
  if (slot < 1 || slot > kSlotCount) {
    return Status::InvalidArgument;
  }
  const Status st = openBridge();
  if (st != Status::Ok) {
    return st;
  }
  const bool ok = flash_.eraseSector(slotFlashOffset(slot) + kSlotHeaderOffset) &&
                  flash_.eraseSector(slotFlashOffset(slot) + kSlotUserOffset);
  closeBridge();
  return ok ? Status::Ok : Status::FlashEraseFailed;
}

Status Board::writeUserData(uint8_t slot, const void* data, uint32_t size,
                            uint32_t modelInfoCrc32) {
  if (slot < 1 || slot > kSlotCount || data == nullptr ||
      size > kSlotUserSize - kFlashSectorSize) {
    return Status::InvalidArgument;
  }
  const Status st = openBridge();
  if (st != Status::Ok) {
    return st;
  }
  const uint32_t base = slotFlashOffset(slot) + kSlotUserOffset;
  UserDataHeader header;
  header.magic = UserDataHeader::kMagic;
  header.size = size;
  header.payloadCrc32 = crc32(static_cast<const uint8_t*>(data), size);
  header.modelInfoCrc32 = modelInfoCrc32;
  header.headerCrc32 = crc32(reinterpret_cast<const uint8_t*>(&header),
                             offsetof(UserDataHeader, headerCrc32));
  // Layout: header alone in the first sector, payload from the next sector.
  // The header sector is erased first and programmed last, so an
  // interrupted write leaves no valid-looking data behind.
  bool ok = flash_.eraseSector(base) &&
            flash_.write(base + kFlashSectorSize,
                         static_cast<const uint8_t*>(data), size);
  ok = ok && flash_.programPage(base, reinterpret_cast<const uint8_t*>(&header),
                                sizeof(header));
  closeBridge();
  return ok ? Status::Ok : Status::FlashProgramFailed;
}

Status Board::readUserData(uint8_t slot, void* data, uint32_t capacity,
                           uint32_t& size, uint32_t modelInfoCrc32) {
  size = 0;
  if (slot < 1 || slot > kSlotCount || data == nullptr) {
    return Status::InvalidArgument;
  }
  const Status st = openBridge();
  if (st != Status::Ok) {
    return st;
  }
  const uint32_t base = slotFlashOffset(slot) + kSlotUserOffset;
  UserDataHeader header;
  flash_.read(base, reinterpret_cast<uint8_t*>(&header), sizeof(header));
  const bool headerOk =
      header.magic == UserDataHeader::kMagic &&
      header.headerCrc32 == crc32(reinterpret_cast<const uint8_t*>(&header),
                                  offsetof(UserDataHeader, headerCrc32)) &&
      header.modelInfoCrc32 == modelInfoCrc32 && header.size <= capacity;
  if (!headerOk) {
    closeBridge();
    return Status::StoreEmpty;
  }
  flash_.read(base + kFlashSectorSize, static_cast<uint8_t*>(data), header.size);
  closeBridge();
  if (crc32(static_cast<const uint8_t*>(data), header.size) !=
      header.payloadCrc32) {
    return Status::FlashVerifyFailed;
  }
  size = header.size;
  return Status::Ok;
}

}  // namespace cbb15
