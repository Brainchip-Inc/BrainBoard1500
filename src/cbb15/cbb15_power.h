// BrainBoard1500 - Texas Instruments INA2227 dual-channel power monitor.
//
// BB15 rev 2 wiring (measured on the board):
//   channel 1: 0.8 V AKD1500 core rail (VDD08), 10 mOhm shunt
//   channel 2: 1.8 V AKD1500 I/O rail (VDE18), 10 mOhm shunt
//   address 0x40, EN driven by expander P1 (INA_EN, BB15 datasheet)
//
// Register map (INA2227 datasheet SBOSAL5, section 7):
//   0x00/0x08 SHUNT_VOLTAGE   0x01/0x09 BUS_VOLTAGE   0x02/0x0A CURRENT
//   0x03/0x0B POWER           0x04/0x0C ENERGY (32 bit) 0x05/0x0D CALIBRATION
//   0x10 CONFIG1  0x11 CONFIG2  0x12 FLAGS  0x7E MANUFACTURER_ID ("TI")
//   0x7F DEVICE_ID
//
// The power is computed from the raw shunt and bus voltages, which needs no
// calibration register and has the best resolution:
//   shunt LSB = 625 nV (ADCRANGE = 1, +/-20.48 mV), bus LSB = 1.6 mV.
#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace cbb15 {

enum class Rail : uint8_t {
  Core0V8 = 0,  // INA2227 channel 1
  Io1V8 = 1,    // INA2227 channel 2
};

struct RailReading {
  float busVolts = 0.0f;
  float milliamps = 0.0f;
  float milliwatts = 0.0f;
};

class PowerMonitor {
 public:
  static constexpr uint16_t kManufacturerId = 0x5449;  // "TI"

  // Probes the device and writes CONFIG1, CONFIG2 and the calibration.
  // Must be called again after every BB15 power cycle: the INA2227 is
  // powered down with the rails and returns with its reset defaults.
  bool begin(TwoWire& wire, uint8_t address, float shuntOhms = 0.010f);
  bool started() const { return started_; }

  bool read(Rail rail, RailReading& out);
  // Convenience: power of a rail in milliwatts, NAN on I2C failure.
  float milliwatts(Rail rail);

  uint16_t manufacturerId();
  uint16_t deviceId();

 private:
  bool write16(uint8_t reg, uint16_t value);
  bool read16(uint8_t reg, uint16_t& value);

  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  float shuntOhms_ = 0.010f;
  bool started_ = false;
};

}  // namespace cbb15
