// BrainBoard1500 - small helpers shared by the example sketches.
#pragma once

#include <Arduino.h>

#include "cbb15_akida.h"
#include "cbb15_board.h"
#include "cbb15_console.h"

namespace cbb15 {

enum class LedColor : uint8_t { Off, Red, Green, Blue, Yellow, White };

// Nicla Vision RGB LED (active low).
void setLed(LedColor color);

// Reports the error on Serial and the OLED, then blinks the red LED forever.
[[noreturn]] void halt(Console& console, const char* what, Status status);

// Standard start of every example: console, title, board bring-up with the
// SPI link test (AKD1500 manufacturer id), Akida runtime. Halts on error.
void startBoard(Console& console, Board& board, Akida& runtime,
                const char* title, const BoardConfig& config = BoardConfig());

// Loads a model into the AKD1500 and logs where it came from. Halts on
// error. Slot 0 pushes the program over SPI, slots 1..4 boot it from flash.
void loadModel(Console& console, Akida& runtime, const Model& model,
               uint8_t slot, bool checkSlotHeader = true);

// AKD1500 duty cycle, the same definition as in 09_MaxDutyCycle: share of
// the elapsed time in which the AKD1500 has work in its pipeline,
// duty = 1 - idle / elapsed, evaluated over windows of windowMs and capped at
// 100 %. For examples with one job in flight at a time, the busy time is the
// sum of the pure inference times (InferenceResult::computeUs, measured by
// the AKD1500 clock counter); sensor capture, preprocessing and the SPI
// transfer of inputs and outputs count as idle.
class DutyCycle {
 public:
  explicit DutyCycle(uint32_t windowMs = 2000) : windowMs_(windowMs) {}
  void add(uint32_t busyUs) { busyUs_ += busyUs; }
  // Percentage of the last complete window, or -1 before the first one.
  float percent();

 private:
  uint32_t windowMs_;
  uint32_t windowStartMs_ = 0;
  uint64_t busyUs_ = 0;
  float last_ = -1.0f;
};

// Formats "AKD duty 5.6%" (or "AKD duty ..." before the first window).
void showDuty(Console& console, DutyCycle& duty, uint8_t row);

// Power of the AKD1500 0.8 V core rail in milliwatts (NAN if unavailable).
float corePowerMilliwatts(Board& board);

// Formats "0V8 12.3 mW" (or "0V8 n/a") into a dashboard row.
void showPower(Console& console, Board& board, uint8_t row);

}  // namespace cbb15
