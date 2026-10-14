// BrainBoard1500 - output to the USB serial port and to a 128x64 SSD1306 OLED.
//
// Every message is sent to both outputs:
//   logf()     one line on Serial, and the same line scrolled into the OLED
//              (rows 1..7, row 0 is the title). Console is also a Print, so
//              print()/println() and every API that takes a Print* (for
//              example Board::begin) end up on both outputs, line by line.
//   setRow()   + publish(): a fixed "dashboard" on the OLED. publish()
//              redraws the OLED (rate limited) and prints the same rows as
//              one line on Serial, so both outputs always show the same.
//
// The OLED shares the Wire bus with the BB15 expander (0x43) and INA2227
// (0x40). It uses the Adafruit_SSD1306 and Adafruit_GFX libraries.
#pragma once

#include <Arduino.h>
#include <Wire.h>

class Adafruit_SSD1306;

namespace cbb15 {

class Console : public Print {
 public:
  static constexpr uint8_t kRows = 8;
  static constexpr uint8_t kColumns = 21;  // 6x8 font on 128 pixels

  // Starts Serial (waits up to serialWaitMs for a monitor) and the OLED.
  // Returns false if the display does not answer; Serial still works.
  bool begin(TwoWire& wire, uint8_t address, unsigned long baud = 115200,
             uint32_t serialWaitMs = 3000);
  bool hasDisplay() const { return display_ != nullptr; }

  void setTitle(const char* title);
  void logf(const char* format, ...) __attribute__((format(printf, 2, 3)));

  void setRow(uint8_t row, const char* format, ...)
      __attribute__((format(printf, 3, 4)));
  void clearRows();
  // Redraws the OLED (at most every minIntervalMs unless forced) and prints
  // the rows to Serial.
  void publish(bool force = false, uint32_t minIntervalMs = 200);
  void setSerialEcho(bool enabled) { serialEcho_ = enabled; }
  // The OLED content is rotated by 180 degrees by default. Call before
  // begin(), or call begin() again, to change it.
  void setRotate180(bool enabled) { rotate180_ = enabled; }
  // Stepwise OLED updates for loops that must not stall. A full update
  // through Adafruit_SSD1306::display() blocks for about 50 ms: on mbed every
  // Wire.setClock() call costs 11.6 ms (measured), and the library switches
  // the clock twice per transfer. When enabled, publish() (without force)
  // draws the frame into RAM and sends 64 bytes per call at the bus clock,
  // without any clock switch (about 6 ms), so a new frame is complete after
  // 16 calls. publish(true), logf() and setTitle() still update the whole
  // display at once.
  void setPagedDisplay(bool enabled) { paged_ = enabled; }

  // Print interface: characters are collected until a newline.
  size_t write(uint8_t c) override;
  using Print::write;

 private:
  void render();
  void draw();
  void sendStep(uint8_t step);
  void emitLine(const char* line);

  Adafruit_SSD1306* display_ = nullptr;
  char rows_[kRows][kColumns + 1] = {};
  uint32_t lastRenderMs_ = 0;
  bool serialEcho_ = true;
  bool rotate180_ = true;
  bool paged_ = false;
  uint8_t nextStep_ = 16;  // 16 = no stepwise transfer in progress
  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  char pending_[kColumns * 4 + 1] = {};
  uint8_t pendingLength_ = 0;
};

}  // namespace cbb15
