// BrainBoard1500 - board constants for the BrainBoard1500 (BB15) rev 2.
//
// Sources of the values in this file:
//   - BB15 datasheet rev 2.1 (pinout, expander map, bring-up sequence, timing)
//   - Arduino Nicla Vision datasheet (J1/J2 header to Arduino pin mapping)
//   - AKD1500 datasheet V1.4, public (memory map, system configuration
//     registers)
//   - Akida Engine 2.19.3 sources (SPI slave protocol, SPI master registers)
//   - Public BB15 Arduino library (Neuromorphyx, GitHub): the guard band
//     around switching the S2M flash bridge (kS2mPhaseGapMs = 250 ms)
//   - Measured on the board: the INA2227 address 0x40, its
//     channel assignment and the 10 mOhm shunts, and the power-up settle
//     time after A8 HIGH.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cbb15 {

constexpr const char* kLibraryVersion = "1.1.0";

// ---------------------------------------------------------------------------
// Host pins (Arduino Nicla Vision stacked on BB15 rev 2).
//
// BB15 pad | BB15 signal         | Nicla Vision header | Arduino pin
// ---------+---------------------+---------------------+------------
// A1       | INT (expander ~INT) | J1-1 D0             | 0
// A3       | AKIDA_CS            | J1-3 SS             | 7
// A4/A5/A6 | MOSI / MISO / SCK   | J1-4/5/6            | SPI
// A8       | RESET (power enable)| J1-8 A0             | 15 (A0)
// B5       | TT_MODE_CS (bridge) | J2-5 D3             | 3
// B1/B2    | SDA / SCL           | J2-1/2              | Wire
//
// BB15 A8 "RESET" enables the on-board power sequencer: HIGH switches the
// 0.8 V and 1.8 V rails on, LOW (on-board pull-down) switches them off.
// ---------------------------------------------------------------------------
struct HostPins {
  uint8_t boardPower = 15;  // A0 on Nicla Vision (J1-8)
  uint8_t akidaCs = 7;      // SS on Nicla Vision (J1-3)
  uint8_t bridgeCs = 3;     // D3 on Nicla Vision (J2-5)
  uint8_t interrupt = 0;    // D0 on Nicla Vision (J1-1)
};

// ---------------------------------------------------------------------------
// I2C devices on the shared Wire bus.
// ---------------------------------------------------------------------------
constexpr uint8_t kExpanderAddress = 0x43;  // PI4IOE5V6408 (strap R14)
constexpr uint8_t kPowerMonitorAddress = 0x40;  // INA2227 (measured)
constexpr uint8_t kDisplayAddress = 0x3C;  // SSD1306 128x64 OLED (user wiring)

// PI4IOE5V6408 port assignment on BB15 rev 2.
namespace expander_pin {
constexpr uint8_t kSpiModeSelect = 0;  // P0: SPI_MODE_SEL, LOW = single SPI
constexpr uint8_t kPowerMonitorEnable = 1;  // P1: INA_EN, HIGH = INA2227 on
constexpr uint8_t kAkidaInterrupt = 2;  // P2: AKD_INT (input from AKD1500)
constexpr uint8_t kAkidaSleep = 3;  // P3: AKD_SLEEP, HIGH = sleep
constexpr uint8_t kAkidaReset = 4;  // P4: AKD_RST, LOW = reset asserted
// P5..P7 are not described in the BB15 datasheet. The library does not use
// them and leaves them at the PI4IOE5V6408 power-on state (input, pull-down).
}  // namespace expander_pin

// ---------------------------------------------------------------------------
// Clocks and timing (BB15 datasheet 5.1.2 "Reference bring-up values").
// ---------------------------------------------------------------------------
constexpr uint32_t kAkidaSpiClockHz = 25000000;  // host <-> AKD1500 SPI slave
constexpr uint32_t kBridgeSpiClockHz = 2000000;  // host <-> flash via S2M
constexpr uint32_t kI2cClockHz = 100000;
constexpr uint32_t kPowerUpSettleMs = 150;  // after A8 HIGH (measured)
constexpr uint32_t kResetAssertMs = 5;
constexpr uint32_t kResetReleaseSettleMs = 10;
constexpr uint32_t kLinkSettleMs = 50;
constexpr uint32_t kSleepEnterSettleMs = 1;
constexpr uint32_t kSleepExitSettleMs = 10;
// Guard band around switching the SPI slave-to-master (S2M) bridge, as in
// the public BB15 library (kS2mPhaseGapMs).
constexpr uint32_t kBridgeSwitchSettleMs = 250;

// ---------------------------------------------------------------------------
// AKD1500 identification.
// The Akida IP version register (top level + 0x0) is
// [31:24] vendor id | [23:16] product id | [15:8] major | [7:0] minor.
// ---------------------------------------------------------------------------
constexpr uint8_t kBrainChipVendorId = 0xBC;
constexpr uint32_t kExpectedIpVersion = 0xBCA10309;

// ---------------------------------------------------------------------------
// AKD1500 system registers used by this library (AKD1500 datasheet 6).
// ---------------------------------------------------------------------------
constexpr uint32_t kRegChipInfo = 0xFCE00010;
constexpr uint32_t kRegControlSignals = 0xFCE00018;
constexpr uint32_t kControlEnSpiS2m = 1u << 16;
constexpr uint32_t kControlSpimDiSwap = 1u << 21;

// SPI master (flash controller) register block.
constexpr uint32_t kSpimBase = 0xFCF20000;
constexpr uint32_t kSpimCtrlr0 = kSpimBase + 0x00;
constexpr uint32_t kSpimSsienr = kSpimBase + 0x08;
constexpr uint32_t kSpimSer = kSpimBase + 0x10;
constexpr uint32_t kSpimBaudr = kSpimBase + 0x14;
constexpr uint32_t kSpimSpiCtrlr0 = kSpimBase + 0xF4;
constexpr uint32_t kSpimDdrDriveEdge = kSpimBase + 0xF8;
constexpr uint32_t kSpimXipModeBits = kSpimBase + 0xFC;
constexpr uint32_t kSpimXipIncrInst = kSpimBase + 0x100;

// ---------------------------------------------------------------------------
// External model flash layout.
//
// The flash is split into four equal 1 MiB slots. The AKD1500 reads a slot
// directly through its SPI master window at 0x8000_0000 (AKD1500 memory map:
// "External SPI Master flash / memory direct access").
//
//   slot 0 = no flash: the model is pushed into the AKD1500 over SPI
//   slot 1 = flash offset 0x000000
//   slot 2 = flash offset 0x100000 (1 MiB)
//   slot 3 = flash offset 0x200000 (2 MiB)
//   slot 4 = flash offset 0x300000 (3 MiB)
//
// Inside every slot:
//   +0x00000  slot header (one 4 KiB sector, see cbb15_slot.h)
//   +0x01000  program data of the model (what the AKD1500 reads)
//   +0xF0000  user area, 64 KiB (used to store learned weights)
// ---------------------------------------------------------------------------
constexpr uint8_t kSlotCount = 4;
constexpr uint32_t kSlotSize = 0x100000;
constexpr uint32_t kSlotHeaderOffset = 0x00000;
constexpr uint32_t kSlotDataOffset = 0x01000;
constexpr uint32_t kSlotUserOffset = 0xF0000;
constexpr uint32_t kSlotUserSize = kSlotSize - kSlotUserOffset;
constexpr uint32_t kSlotMaxDataSize = kSlotUserOffset - kSlotDataOffset;
constexpr uint32_t kFlashXipBase = 0x80000000;
constexpr uint32_t kFlashSectorSize = 4096;
constexpr uint32_t kFlashPageSize = 256;

inline uint32_t slotFlashOffset(uint8_t slot) {
  return static_cast<uint32_t>(slot - 1) * kSlotSize;
}

}  // namespace cbb15
