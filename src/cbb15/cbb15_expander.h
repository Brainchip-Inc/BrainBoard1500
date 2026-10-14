// BrainBoard1500 - driver for the Diodes PI4IOE5V6408 8-bit I2C I/O expander.
//
// Register map (PI4IOE5V6408 datasheet, table 6):
//   0x01 device id and control   0x03 I/O direction (1 = output)
//   0x05 output port             0x07 output high-impedance (1 = high-Z)
//   0x09 input default state     0x0B pull-up/down enable
//   0x0D pull-up/down select     0x0F input status
//   0x11 interrupt mask (1 = masked)  0x13 interrupt status (read clears)
#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace cbb15 {

class Expander {
 public:
  static constexpr uint8_t kRegDeviceId = 0x01;
  static constexpr uint8_t kRegDirection = 0x03;
  static constexpr uint8_t kRegOutput = 0x05;
  static constexpr uint8_t kRegOutputHighZ = 0x07;
  static constexpr uint8_t kRegPullEnable = 0x0B;
  static constexpr uint8_t kRegPullSelect = 0x0D;
  static constexpr uint8_t kRegInputStatus = 0x0F;
  static constexpr uint8_t kRegInterruptMask = 0x11;
  static constexpr uint8_t kRegInterruptStatus = 0x13;

  void attach(TwoWire& wire, uint8_t address);
  bool present();

  // Writes the complete BB15 port configuration. The output register is
  // written before the direction register so that every output starts with
  // its intended level the moment it becomes an output.
  //   outputs: P0 SPI_MODE_SEL, P1 INA_EN, P3 AKD_SLEEP, P4 AKD_RST
  //   inputs:  P2 AKD_INT (pull-down enabled)
  //   P5..P7:  unused, left at the power-on state (input, pull-down)
  bool configureBb15(uint8_t outputLevels);

  bool setOutput(uint8_t pin, bool high);
  bool outputLevels(uint8_t& levels);
  bool readInputs(uint8_t& levels);
  bool readInterruptStatus(uint8_t& status);
  bool setInterruptMask(uint8_t mask);

  bool writeRegister(uint8_t reg, uint8_t value);
  bool readRegister(uint8_t reg, uint8_t& value);

 private:
  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  uint8_t outputCache_ = 0;
};

}  // namespace cbb15
