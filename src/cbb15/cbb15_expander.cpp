#include "cbb15_expander.h"

#include "cbb15_config.h"

namespace cbb15 {

namespace {

constexpr uint8_t pinBit(uint8_t pin) { return static_cast<uint8_t>(1u << pin); }

constexpr uint8_t kOutputMask =
    pinBit(expander_pin::kSpiModeSelect) | pinBit(expander_pin::kPowerMonitorEnable) |
    pinBit(expander_pin::kAkidaSleep) | pinBit(expander_pin::kAkidaReset);
// P5..P7: not used by this library (see cbb15_config.h).
constexpr uint8_t kUnusedMask = pinBit(5) | pinBit(6) | pinBit(7);

}  // namespace

void Expander::attach(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
}

bool Expander::present() {
  if (wire_ == nullptr) {
    return false;
  }
  wire_->beginTransmission(address_);
  return wire_->endTransmission() == 0;
}

bool Expander::configureBb15(uint8_t outputLevels) {
  outputCache_ = outputLevels & kOutputMask;
  return writeRegister(kRegOutput, outputCache_) &&
         writeRegister(kRegDirection, kOutputMask) &&
         writeRegister(kRegOutputHighZ, static_cast<uint8_t>(~kOutputMask)) &&
         writeRegister(kRegPullSelect, 0x00) &&  // pull-down
         // Weak pull-downs on the inputs: AKD_INT stays defined while the
         // AKD1500 is in reset, and the unused pins keep the expander's
         // power-on state (input, pull-down enabled).
         writeRegister(kRegPullEnable,
                       kUnusedMask | pinBit(expander_pin::kAkidaInterrupt)) &&
         writeRegister(kRegInterruptMask, 0xFF);  // all interrupts masked
}

bool Expander::setOutput(uint8_t pin, bool high) {
  if (pin > 7) {
    return false;
  }
  const uint8_t next = high ? static_cast<uint8_t>(outputCache_ | pinBit(pin))
                            : static_cast<uint8_t>(outputCache_ & ~pinBit(pin));
  if (!writeRegister(kRegOutput, next)) {
    return false;
  }
  outputCache_ = next;
  return true;
}

bool Expander::outputLevels(uint8_t& levels) {
  return readRegister(kRegOutput, levels);
}

bool Expander::readInputs(uint8_t& levels) {
  return readRegister(kRegInputStatus, levels);
}

bool Expander::readInterruptStatus(uint8_t& status) {
  return readRegister(kRegInterruptStatus, status);
}

bool Expander::setInterruptMask(uint8_t mask) {
  return writeRegister(kRegInterruptMask, mask);
}

bool Expander::writeRegister(uint8_t reg, uint8_t value) {
  if (wire_ == nullptr) {
    return false;
  }
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool Expander::readRegister(uint8_t reg, uint8_t& value) {
  if (wire_ == nullptr) {
    return false;
  }
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) {
    return false;
  }
  if (wire_->requestFrom(static_cast<int>(address_), 1) != 1) {
    return false;
  }
  value = static_cast<uint8_t>(wire_->read());
  return true;
}

}  // namespace cbb15
