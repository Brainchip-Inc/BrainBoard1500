#include "cbb15_spi_link.h"

#include <stdio.h>
#include <string.h>

#include "cbb15_spi_fast.h"

namespace cbb15 {

void SpiLink::begin(SPIClass& spi, uint8_t akidaCs, uint8_t bridgeCs,
                    uint32_t akidaClockHz, uint32_t bridgeClockHz) {
  spi_ = &spi;
  akidaCs_ = akidaCs;
  bridgeCs_ = bridgeCs;
  akidaClockHz_ = akidaClockHz;
  bridgeClockHz_ = bridgeClockHz;
  // Both chip selects are active low and must idle high.
  pinMode(akidaCs_, OUTPUT);
  digitalWrite(akidaCs_, HIGH);
  pinMode(bridgeCs_, OUTPUT);
  digitalWrite(bridgeCs_, HIGH);
  if (!started_) {
    spi_->begin();
    // MbedSPI caches the last SPISettings in its wrapper and only reprograms
    // the peripheral when beginTransaction() gets different settings. After
    // end() + begin() (BB15 power cycle) the new mbed::SPI object runs at its
    // 1 MHz default while the cache still holds 25 MHz, so the clock would
    // never be set again. A transaction with settings that cannot match
    // invalidates the cache; no chip select is active here.
    spi_->beginTransaction(SPISettings(1000000, LSBFIRST, SPI_MODE3));
    spi_->endTransaction();
    // Block transfers instead of mbed's byte-by-byte path (cbb15_spi_fast.h).
    // Pins and peripheral clock come from spi_->begin() above.
    fast_ = spi_fast::begin(*spi_);
    started_ = true;
  }
  selected_ = false;
  burstFast_ = false;
}

void SpiLink::end() {
  if (started_) {
    spi_fast::end();
    fast_ = false;
    // MbedSPI::begin() is idempotent: a power cycle of the BB15 needs a
    // matching end() first, otherwise the bus is never re-initialised.
    spi_->end();
    started_ = false;
  }
  selected_ = false;
  burstFast_ = false;
}

void SpiLink::chip_select(uint32_t /*slaveId*/, bool active) {
  if (active && !selected_) {
    spi_->beginTransaction(SPISettings(akidaClockHz_, MSBFIRST, SPI_MODE0));
    // Before the chip select goes low: reprogramming (after a bridge transfer
    // at 2 MHz) briefly disables the peripheral.
    burstFast_ = fast_ && spi_fast::configure(akidaClockHz_);
    digitalWrite(akidaCs_, LOW);
    selected_ = true;
  } else if (!active && selected_) {
    digitalWrite(akidaCs_, HIGH);
    spi_->endTransaction();
    selected_ = false;
  }
}

void SpiLink::read(uint8_t* data, size_t size) {
  // The engine always calls read() inside chip_select(true/false).
  if (burstFast_) {
    // A failed fast transfer is not repeated on the mbed path: part of it
    // is already on the wire. The engine sees the wrong data instead.
    if (!spi_fast::read(data, size)) {
      ++fastErrors_;
    }
    return;
  }
  memset(data, 0, size);
  spi_->transfer(data, size);
}

void SpiLink::write(const uint8_t* data, size_t size) {
  if (burstFast_) {
    if (!spi_fast::write(data, size)) {
      ++fastErrors_;
    }
    return;
  }
  // SPIClass::transfer() works in place, so the const source is copied
  // into a scratch buffer chunk by chunk.
  while (size > 0) {
    const size_t n = size < kChunk ? size : kChunk;
    memcpy(scratch_, data, n);
    spi_->transfer(scratch_, n);
    data += n;
    size -= n;
  }
}

void SpiLink::bridgeTransfer(uint8_t* buffer, size_t size) {
  chip_select(0, false);
  // With the fast path active, the bridge uses it too. Handing the bus back
  // to mbed's byte path in between is not safe: the HAL leaves the
  // peripheral disabled and TSIZE set, which the byte path does not expect.
  if (fast_ && spi_fast::configure(bridgeClockHz_)) {
    digitalWrite(bridgeCs_, LOW);
    while (size > 0) {
      const size_t n = size < kChunk ? size : kChunk;
      memcpy(scratch_, buffer, n);
      if (!spi_fast::transfer(scratch_, buffer, n)) {
        ++fastErrors_;
        break;
      }
      buffer += n;
      size -= n;
    }
    digitalWrite(bridgeCs_, HIGH);
    return;
  }
  spi_->beginTransaction(SPISettings(bridgeClockHz_, MSBFIRST, SPI_MODE0));
  digitalWrite(bridgeCs_, LOW);
  spi_->transfer(buffer, size);
  digitalWrite(bridgeCs_, HIGH);
  spi_->endTransaction();
}

const char* SpiLink::describe() {
  if (!fast_) {
    snprintf(describe_, sizeof(describe_), "SPI mbed byte path");
  } else {
    snprintf(describe_, sizeof(describe_), "SPI %s block %lu MHz",
             spi_fast::name(),
             static_cast<unsigned long>(
                 (spi_fast::configure(akidaClockHz_) ? spi_fast::sckHz() : 0) /
                 1000000));
  }
  return describe_;
}

}  // namespace cbb15
