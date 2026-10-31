// BrainBoard1500 - BrainBoard1500 (BB15) rev 2 board control.
//
// Bring-up follows BB15 datasheet 5.1.1:
//   1. drive A8 (RESET) high so the power sequencer brings up 0V8 and 1V8
//   2. I2C at 100 kHz, probe the PI4IOE5V6408 at 0x43, write the port setup
//      with AKD_RST asserted (P4 low), AKD_SLEEP low (awake), INA_EN high
//   3. AKIDA_CS and TT_MODE_CS idle high
//   4. hold reset 5 ms, release it, wait 10 ms
//   5. SPI link test: read the AKD1500 manufacturer id (vendor byte of the
//      Akida IP version register, 0xBC = BrainChip) with a raw SPI read
//   6. create the Akida Engine SPI driver, check the IP version 0xBCA10309,
//      settle 50 ms, check again, settle 50 ms
//   7. INA2227 configuration and the flash runtime profile
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <memory>

#include "cbb15_config.h"
#include "cbb15_expander.h"
#include "cbb15_flash.h"
#include "cbb15_power.h"
#include "cbb15_spi_link.h"
#include "cbb15_types.h"

namespace akida {
class Akd1500SpiDriver;
class HardwareDriver;
}  // namespace akida

namespace cbb15 {

struct BoardConfig {
  HostPins pins;
  SPIClass* spi = &SPI;
  TwoWire* wire = &Wire;
  uint32_t akidaSpiClockHz = kAkidaSpiClockHz;  // 25 MHz
  uint32_t bridgeSpiClockHz = kBridgeSpiClockHz;  // 2 MHz
  uint8_t expanderAddress = kExpanderAddress;
  uint8_t powerMonitorAddress = kPowerMonitorAddress;
  // BB15 rev 2 carries a Renesas AT25SL321, rev 1 a Winbond W25Q64JW (both
  // read out on the boards; the datasheet lists both as supported).
  // Rev 1 is obsolete but still selectable.
  FlashChip flashChip = FlashChip::RenesasAT25SL321;
  // Skip the flash probe (JEDEC id and quad-enable check over the S2M
  // bridge, about 1 s): the runtime profile is simply written. This is safe
  // once the flash has been probed one time, which the model flasher does.
  bool assumeFlashReady = true;
};

class Board {
 public:
  Board();
  ~Board();

  // Powers the BB15 up and runs the full bring-up. Optional log output.
  Status begin(const BoardConfig& config = BoardConfig(), Print* log = nullptr);
  bool started() const { return started_; }

  // Switches the BB15 rails off (AKD1500, flash and INA2227 lose power).
  // begin() brings the board back; models and learned weights in the
  // AKD1500 are lost, flash content survives.
  void powerOff();

  // AKD_SLEEP (P3). Sleep keeps the model in the AKD1500 SRAM.
  bool sleep();
  bool wake();

  // SPI link test: raw read of the Akida IP version register, returns the
  // manufacturer (vendor) id byte. 0xBC means BrainChip.
  uint8_t readManufacturerId(uint32_t* ipVersion = nullptr);
  uint32_t readIpVersion();
  uint8_t chipRevision();  // AKD1500 chip info register, version field

  // --- External flash -----------------------------------------------------
  // Full flash probe (JEDEC id, quad enable). The flasher calls this once.
  Status probeFlash(uint32_t* jedec = nullptr);
  // Writes only the SPI master XIP configuration (the fast path).
  void applyFlashRuntimeProfile();
  const FlashProfile& currentFlashProfile() const {
    return flashProfile(config_.flashChip);
  }

  // Writes a model into slot 1..4 and verifies it. Skips the write if the
  // slot already holds exactly this model (unless force is set).
  Status writeModelToSlot(uint8_t slot, const Model& model, Print* log = nullptr,
                          bool force = false);
  Status readSlotHeader(uint8_t slot, SlotHeader& header);
  // True if the slot header describes exactly this model.
  Status checkSlot(uint8_t slot, const Model& model);
  Status eraseSlot(uint8_t slot);
  // Copies the model's program data from slot 1..4 into AKD1500 memory at
  // `address` (the SRAM, as the engine addresses it) through the S2M bridge,
  // then reads the copy back. Nothing is copied when that memory already
  // holds exactly this data. FlashVerifyFailed unless both the flash data
  // and the copy match the model's data CRC.
  Status copySlotData(uint8_t slot, const Model& model, uint32_t address);

  // User area of a slot (64 KiB), used for learned weights.
  Status writeUserData(uint8_t slot, const void* data, uint32_t size,
                       uint32_t modelInfoCrc32);
  Status readUserData(uint8_t slot, void* data, uint32_t capacity,
                      uint32_t& size, uint32_t modelInfoCrc32);

  // --- Accessors --------------------------------------------------------
  akida::HardwareDriver* driver();
  SpiLink& link() { return link_; }
  Expander& expander() { return expander_; }
  PowerMonitor& power() { return power_; }
  FlashBridge& flash() { return flash_; }
  const BoardConfig& config() const { return config_; }

 private:
  Status checkLink(Print* log);
  uint32_t rawRead32(uint32_t address);
  // CRC-32 of `size` bytes of AKD1500 memory, read over the SPI slave.
  uint32_t akidaMemoryCrc32(uint32_t address, uint32_t size);
  Status openBridge();
  void closeBridge();

  BoardConfig config_;
  SpiLink link_;
  Expander expander_;
  PowerMonitor power_;
  FlashBridge flash_;
  std::unique_ptr<akida::Akd1500SpiDriver> driver_;
  bool started_ = false;
};

}  // namespace cbb15
