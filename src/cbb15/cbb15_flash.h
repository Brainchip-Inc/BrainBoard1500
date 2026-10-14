// BrainBoard1500 - BB15 model flash access.
//
// The flash is wired to the AKD1500 SPI master only. The host reaches it in
// two ways:
//   1. Through the AKD1500 "SPI slave-to-master" (S2M) pin mux (AKD1500
//      datasheet 3.8): setting EN_SPI_S2M (system configuration register
//      0x18, bit 16) connects SPI_S_2MCS0_N / SCK / IO0 / IO1 of the slave
//      port straight to the SPI master pins. The host then speaks plain
//      single-bit SPI NOR commands to the flash, using TT_MODE_CS as chip
//      select. This is used to probe, erase, program and read the flash.
//   2. The AKD1500 itself reads the flash in XIP mode through its SPI master
//      (window 0x8000_0000). This needs the SPI master "runtime profile"
//      (read opcode, address phase, dummy cycles) to match the flash chip.
#pragma once

#include <Arduino.h>

#include "cbb15_spi_link.h"
#include "cbb15_types.h"
#include "infra/hardware_driver.h"

namespace cbb15 {

enum class FlashChip : uint8_t {
  RenesasAT25SL321 = 0,  // BB15 rev 2: 32 Mbit, 1.8 V, JEDEC 1F 42 16
  WinbondW25Q64JW,       // BB15 rev 1 (obsolete, measured): 64 Mbit, JEDEC EF 60 17
  Unknown,
};

struct FlashProfile {
  FlashChip chip;
  const char* name;
  uint8_t manufacturerId;
  uint8_t memoryType;
  uint8_t capacityId;
  uint32_t capacityBytes;
  // XIP read used by the AKD1500 SPI master.
  uint8_t readOpcode;    // 0x6B = quad output fast read
  uint8_t transferType;  // SPI_CTRLR0.TRANS_TYPE: 0 = instruction and address single
  uint8_t waitCycles;    // dummy clocks between address and data
  // Quad enable bit (needed by the quad read).
  uint8_t qeReadCommand;   // read status register 2
  uint8_t qeWriteCommand;  // write status register 2
  uint8_t qeMask;
};

const FlashProfile& flashProfile(FlashChip chip);

class FlashBridge {
 public:
  void attach(SpiLink& link, akida::HardwareDriver& driver);

  // Writes the SPI master XIP configuration for the given chip. This is all
  // that is needed before the AKD1500 can read a model from flash, as long as
  // the chip was probed once (quad enable is non-volatile). Takes < 1 ms.
  void applyRuntimeProfile(const FlashProfile& profile);

  // Full probe through the S2M bridge: JEDEC id, quad enable bit (set if
  // needed), then the runtime profile. Takes about one second because of the
  // bridge switching guard bands.
  Status probe(FlashChip expected, uint32_t* jedecOut = nullptr);

  // S2M session. All functions below require an open session.
  Status open();
  void close();
  bool isOpen() const { return open_; }

  uint32_t readJedec();
  uint8_t readStatus(uint8_t command);
  bool waitReady(uint32_t timeoutMs);
  bool writeEnable();
  bool eraseSector(uint32_t address);
  bool programPage(uint32_t address, const uint8_t* data, size_t size);
  bool read(uint32_t address, uint8_t* data, size_t size);
  // Erases the sectors covering [address, address + size) and programs data.
  bool write(uint32_t address, const uint8_t* data, size_t size);
  bool ensureQuadEnable(const FlashProfile& profile);

 private:
  void command(uint8_t opcode);
  void resetPreamble();

  SpiLink* link_ = nullptr;
  akida::HardwareDriver* driver_ = nullptr;
  bool open_ = false;
  uint32_t savedControl_ = 0;
  uint8_t buffer_[4 + 256];
};

}  // namespace cbb15
