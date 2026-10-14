// BrainBoard1500 example 01 - SPI link test and board check.
//
// Hardware: Arduino Nicla Vision on a BrainBoard1500 (BB15) rev 2,
//           SSD1306 128x64 OLED at I2C address 0x3C (optional).
//
// What it does:
//   1. Powers the BB15 up and tests the SPI connection by reading the
//      AKD1500 manufacturer id (0xBC = BrainChip) at 25 MHz.
//   2. Probes the model flash over the AKD1500 S2M bridge (JEDEC id, quad
//      enable bit) and lists the content of the four flash slots.
//   3. Writes and reads back a 64 KiB pattern in the AKD1500 SRAM to check
//      the data integrity of the SPI link and to measure its throughput.
//   4. Shows the power of the 0.8 V core rail and the 1.8 V I/O rail.
//
// Every line goes to the serial monitor (115200 baud) and to the OLED.

#include <BrainBoard1500.h>

#include <vector>

#include "akida/hardware_device.h"

cbb15::Console console;
cbb15::Board board;
cbb15::Akida runtime;

// A free part of the AKD1500 SRAM (XH_2PMEM, 1 MiB at 0xFC80_0000 seen from
// the SPI host). No model is loaded by this sketch.
constexpr uint32_t kTestAddress = 0xFC880000;
constexpr uint32_t kTestBytes = 64 * 1024;

void slotTable() {
  for (uint8_t slot = 1; slot <= cbb15::kSlotCount; ++slot) {
    cbb15::SlotHeader header;
    const cbb15::Status st = board.readSlotHeader(slot, header);
    if (st == cbb15::Status::Ok) {
      console.logf("slot %u: %s %luK", slot, header.name,
                   static_cast<unsigned long>(header.dataSize / 1024));
    } else {
      console.logf("slot %u: %s", slot, cbb15::statusName(st));
    }
  }
}

void sramTest() {
  std::vector<uint32_t> out(kTestBytes / 4), in(kTestBytes / 4);
  uint32_t x = 0x12345678;
  for (auto& w : out) {  // xorshift pattern
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w = x;
  }
  akida::HardwareDriver* driver = board.driver();
  const uint32_t t0 = micros();
  driver->write(kTestAddress, out.data(), kTestBytes);
  const uint32_t t1 = micros();
  driver->read(kTestAddress, in.data(), kTestBytes);
  const uint32_t t2 = micros();
  size_t errors = 0;
  for (size_t i = 0; i < out.size(); ++i) {
    errors += out[i] != in[i];
  }
  console.logf("SRAM test %s", errors == 0 ? "PASS" : "FAIL");
  console.logf("write %lu KB/s", static_cast<unsigned long>(
                                     kTestBytes * 1000ull / (t1 - t0)));
  console.logf("read  %lu KB/s", static_cast<unsigned long>(
                                     kTestBytes * 1000ull / (t2 - t1)));
  if (errors != 0) {
    console.logf("%u bad words", static_cast<unsigned>(errors));
  }
}

void setup() {
  // This sketch performs the full flash probe instead of the fast path.
  cbb15::BoardConfig config;
  config.assumeFlashReady = false;
  cbb15::startBoard(console, board, runtime, "BB15 link test", config);
  slotTable();
  sramTest();
  delay(3000);
  console.clearRows();
}

void loop() {
  // Repeat the manufacturer id read and show both rails once per second.
  static uint32_t reads = 0, errors = 0;
  for (int i = 0; i < 100; ++i) {
    ++reads;
    errors += board.readManufacturerId() != cbb15::kBrainChipVendorId;
  }
  cbb15::RailReading core, io;
  const bool okCore = board.power().read(cbb15::Rail::Core0V8, core);
  const bool okIo = board.power().read(cbb15::Rail::Io1V8, io);
  console.setRow(1, "AKD1500 id 0x%02X", board.readManufacturerId());
  console.setRow(2, "id reads %lu err %lu", static_cast<unsigned long>(reads),
                 static_cast<unsigned long>(errors));
  if (okCore && okIo) {
    console.setRow(3, "0V8 %.3fV %.1fmA", core.busVolts, core.milliamps);
    console.setRow(4, "0V8 %.1f mW", core.milliwatts);
    console.setRow(5, "1V8 %.3fV %.2fmA", io.busVolts, io.milliamps);
    console.setRow(6, "1V8 %.2f mW", io.milliwatts);
  } else {
    console.setRow(3, "INA2227 n/a");
  }
  console.publish(true);
  delay(1000);
}
