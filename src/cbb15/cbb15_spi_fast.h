// BrainBoard1500 - block transfers on the Nicla Vision SPI (STM32H747).
//
// Why: mbed sends SPI one byte per call. On the H7 every byte restarts the
// master transfer (CR1 |= CSTART) and polls TXP and RXP, so the clock stands
// still between bytes. A 96x96x3 input (27 648 B) took 25 ms at 25 MHz
// instead of about 9 ms of wire time.
//
// This module drives the SPI peripheral with one HAL transfer per burst
// (TSIZE = n, one CSTART). Pins, alternate functions and the peripheral clock
// still come from mbed's SPI.begin(); only CFG1/CFG2 and the data registers
// are touched here.
//
// Measured on BB15 rev 2 + Nicla Vision, VWW, model pushed via SPI, 100 runs:
//   mbed byte path  31.0 ms per inference
//   block transfer  16.0 ms per inference, scores bit-identical
// A DMA variant was measured as well and is exactly as fast (the chip select
// has to toggle after every burst, so the CPU waits either way). It is not
// included.
//
// Set CBB15_SPI_FAST to 0 to use the mbed byte path everywhere.
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <stddef.h>
#include <stdint.h>

#ifndef CBB15_SPI_FAST
#define CBB15_SPI_FAST 1
#endif

namespace cbb15 {
namespace spi_fast {

// Binds the fast path to the peripheral behind `spi` (SPI = SPI4 on
// D8/D9/D10, SPI1 = SPI5 on A5/A6/D6). Call after spi.begin(). Returns false
// (and leaves the mbed path in charge) if the bus is unknown, the kernel clock
// cannot be determined, or CBB15_SPI_FAST is 0.
bool begin(SPIClass& spi);
void end();
bool available();

// Programs mode 0, MSB first and the highest clock <= clockHz. Cheap when
// nothing changed since the last call. Must be called with no chip select
// active: HAL_SPI_Init briefly disables the peripheral.
bool configure(uint32_t clockHz);

// Blocking transfers. read() clocks out zeros. A false return means the
// transfer stopped part-way; the caller must not repeat it on another path.
bool write(const uint8_t* tx, size_t n);
bool transfer(const uint8_t* tx, uint8_t* rx, size_t n);
bool read(uint8_t* rx, size_t n);

uint32_t kernelHz();
uint32_t sckHz();  // what is really on the wire (power-of-two divider)
const char* name();  // "SPI4", "SPI5" or "off"

}  // namespace spi_fast
}  // namespace cbb15
