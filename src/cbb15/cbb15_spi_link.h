// BrainBoard1500 - Arduino SPI transport for the Akida Engine.
//
// The Akida Engine talks to the AKD1500 SPI slave through the
// akida::AbstractSpiDriver interface: it frames every burst itself
// (instruction, 24-bit word address, data) and only needs raw byte
// transfers plus a chip select. This class provides exactly that on the
// Nicla Vision SPI bus with AKIDA_CS, and additionally drives the second
// chip select (TT_MODE_CS) that reaches the BB15 flash while the AKD1500
// SPI slave-to-master (S2M) bridge is enabled.
//
// Transfers use block transfers on the SPI peripheral (cbb15_spi_fast.h)
// when available, otherwise mbed's byte-by-byte SPIClass::transfer().
#pragma once

#include <Arduino.h>
#include <SPI.h>

#include "akd1500/abstract_spi_driver.h"

namespace cbb15 {

class SpiLink final : public akida::AbstractSpiDriver {
 public:
  void begin(SPIClass& spi, uint8_t akidaCs, uint8_t bridgeCs,
             uint32_t akidaClockHz, uint32_t bridgeClockHz);
  void end();
  bool started() const { return started_; }

  // akida::AbstractSpiDriver
  void read(uint8_t* data, size_t size) override;
  void write(const uint8_t* data, size_t size) override;
  void chip_select(uint32_t slaveId, bool active) override;

  // One complete transaction on the bridge chip select (TT_MODE_CS).
  // The buffer is sent and overwritten with the received bytes.
  void bridgeTransfer(uint8_t* buffer, size_t size);

  uint32_t akidaClockHz() const { return akidaClockHz_; }

  // True when the block-transfer path is in use (CBB15_SPI_FAST).
  bool fast() const { return fast_; }
  // Fast transfers that stopped part-way (HAL timeout); should stay 0.
  uint32_t fastErrors() const { return fastErrors_; }
  // One line for the boot log, e.g. "SPI SPI4 block 25 MHz". Call with no
  // chip select active.
  const char* describe();

 private:
  static constexpr size_t kChunk = 512;

  SPIClass* spi_ = nullptr;
  uint8_t akidaCs_ = 0;
  uint8_t bridgeCs_ = 0;
  uint32_t akidaClockHz_ = 0;
  uint32_t bridgeClockHz_ = 0;
  bool started_ = false;
  bool selected_ = false;
  bool fast_ = false;
  bool burstFast_ = false;  // fast path programmed for the current burst
  uint32_t fastErrors_ = 0;
  char describe_[40];
  uint8_t scratch_[kChunk];
};

}  // namespace cbb15
