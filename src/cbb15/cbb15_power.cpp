#include "cbb15_power.h"

#include <math.h>

namespace cbb15 {

namespace {

constexpr uint8_t kRegShunt[2] = {0x00, 0x08};
constexpr uint8_t kRegBus[2] = {0x01, 0x09};
constexpr uint8_t kRegCalibration[2] = {0x05, 0x0D};
constexpr uint8_t kRegConfig1 = 0x10;
constexpr uint8_t kRegConfig2 = 0x11;
constexpr uint8_t kRegManufacturerId = 0x7E;
constexpr uint8_t kRegDeviceId = 0x7F;

// CONFIG1: both channels active, 16 averages, 588 us conversion time for
// bus and shunt, continuous shunt and bus measurement. One channel updates
// every 16 * 2 * 588 us = 19 ms, i.e. both channels every 38 ms.
constexpr uint16_t kConfig1 = (0x3u << 12) |  // ACTIVE_CHANNEL = CH1 | CH2
                              (0x2u << 9) |   // AVG = 16
                              (0x3u << 6) |   // VBUSCT = 588 us
                              (0x3u << 3) |   // VSHCT = 588 us
                              0x7u;           // MODE = continuous shunt + bus
// CONFIG2: ADCRANGE = 1 (+/-20.48 mV) on both channels. With 10 mOhm the
// full scale is 2 A, far above what the AKD1500 draws.
constexpr uint16_t kConfig2 = 0x0003;

constexpr float kShuntLsbVolts = 625e-9f;  // ADCRANGE = 1
constexpr float kBusLsbVolts = 1.6e-3f;

// Calibration for the CURRENT/POWER/ENERGY registers (datasheet eq. 1):
// SHUNT_CAL = 0.00512 / (CURRENT_LSB * R_SHUNT), divided by 4 for
// ADCRANGE = 1. With CURRENT_LSB = 10 uA this library does not depend on
// these registers, but programming them keeps the device fully usable.
constexpr float kCurrentLsbAmps = 10e-6f;

}  // namespace

bool PowerMonitor::begin(TwoWire& wire, uint8_t address, float shuntOhms) {
  wire_ = &wire;
  address_ = address;
  shuntOhms_ = shuntOhms;
  started_ = false;
  if (manufacturerId() != kManufacturerId) {
    return false;
  }
  const uint16_t calibration = static_cast<uint16_t>(
      lroundf(0.00512f / (kCurrentLsbAmps * shuntOhms_) / 4.0f));
  started_ = write16(kRegConfig1, kConfig1) && write16(kRegConfig2, kConfig2) &&
             write16(kRegCalibration[0], calibration) &&
             write16(kRegCalibration[1], calibration);
  return started_;
}

bool PowerMonitor::read(Rail rail, RailReading& out) {
  const uint8_t ch = static_cast<uint8_t>(rail);
  uint16_t shuntRaw = 0;
  uint16_t busRaw = 0;
  if (!started_ || !read16(kRegShunt[ch], shuntRaw) ||
      !read16(kRegBus[ch], busRaw)) {
    return false;
  }
  const float shuntVolts = static_cast<int16_t>(shuntRaw) * kShuntLsbVolts;
  out.busVolts = static_cast<int16_t>(busRaw) * kBusLsbVolts;
  out.milliamps = shuntVolts / shuntOhms_ * 1000.0f;
  out.milliwatts = out.milliamps * out.busVolts;
  return true;
}

float PowerMonitor::milliwatts(Rail rail) {
  RailReading r;
  return read(rail, r) ? r.milliwatts : NAN;
}

uint16_t PowerMonitor::manufacturerId() {
  uint16_t v = 0;
  return read16(kRegManufacturerId, v) ? v : 0;
}

uint16_t PowerMonitor::deviceId() {
  uint16_t v = 0;
  return read16(kRegDeviceId, v) ? v : 0;
}

bool PowerMonitor::write16(uint8_t reg, uint16_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(static_cast<uint8_t>(value >> 8));  // MSB first
  wire_->write(static_cast<uint8_t>(value));
  return wire_->endTransmission() == 0;
}

bool PowerMonitor::read16(uint8_t reg, uint16_t& value) {
  if (wire_ == nullptr) {
    return false;
  }
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) {
    return false;
  }
  if (wire_->requestFrom(static_cast<int>(address_), 2) != 2) {
    return false;
  }
  const uint8_t msb = static_cast<uint8_t>(wire_->read());
  const uint8_t lsb = static_cast<uint8_t>(wire_->read());
  value = static_cast<uint16_t>((msb << 8) | lsb);
  return true;
}

}  // namespace cbb15
