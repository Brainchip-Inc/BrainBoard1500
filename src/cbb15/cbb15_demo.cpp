#include "cbb15_demo.h"

#include <math.h>

namespace cbb15 {

void setLed(LedColor color) {
#if defined(LEDR) && defined(LEDG) && defined(LEDB)
  const bool r = color == LedColor::Red || color == LedColor::Yellow ||
                 color == LedColor::White;
  const bool g = color == LedColor::Green || color == LedColor::Yellow ||
                 color == LedColor::White;
  const bool b = color == LedColor::Blue || color == LedColor::White;
  pinMode(LEDR, OUTPUT);
  pinMode(LEDG, OUTPUT);
  pinMode(LEDB, OUTPUT);
  digitalWrite(LEDR, r ? LOW : HIGH);
  digitalWrite(LEDG, g ? LOW : HIGH);
  digitalWrite(LEDB, b ? LOW : HIGH);
#else
  (void)color;
#endif
}

void halt(Console& console, const char* what, Status status) {
  console.logf("ERROR %s:", what);
  console.logf("%s", statusName(status));
  for (;;) {
    setLed(LedColor::Red);
    delay(200);
    setLed(LedColor::Off);
    delay(800);
  }
}

void startBoard(Console& console, Board& board, Akida& runtime,
                const char* title, const BoardConfig& config) {
  setLed(LedColor::Off);
  console.begin(*config.wire, kDisplayAddress);
  console.setTitle(title);
  console.logf("BrainBoard1500 %s", kLibraryVersion);
  const Status st = board.begin(config, &console);
  if (st != Status::Ok) {
    halt(console, "BB15 start", st);
  }
  const Status rt = runtime.begin(board);
  if (rt != Status::Ok) {
    halt(console, "Akida start", rt);
  }
}

void loadModel(Console& console, Akida& runtime, const Model& model,
               uint8_t slot, bool checkSlotHeader) {
  if (slot == 0) {
    console.logf("load %s via SPI", model.name);
  } else {
    console.logf("load %s from slot %u", model.name, slot);
  }
  const Status st = runtime.load(model, slot, checkSlotHeader, &console);
  if (st != Status::Ok) {
    if (st == Status::SlotEmpty || st == Status::SlotMismatch) {
      console.logf("run 02_ModelFlasher first");
    }
    halt(console, "model load", st);
  }
  console.logf("loaded in %lu ms", static_cast<unsigned long>(runtime.loadTimeMs()));
}

float DutyCycle::percent() {
  const uint32_t now = millis();
  if (windowStartMs_ == 0) {
    windowStartMs_ = now;
    busyUs_ = 0;
    return last_;
  }
  const uint32_t elapsedMs = now - windowStartMs_;
  if (elapsedMs >= windowMs_) {
    last_ = static_cast<float>(busyUs_) / (elapsedMs * 10.0f);  // us / ms -> %
    // A job that straddles the window edge is booked entirely in this window.
    last_ = last_ > 100.0f ? 100.0f : last_;
    windowStartMs_ = now;
    busyUs_ = 0;
  }
  return last_;
}

void showDuty(Console& console, DutyCycle& duty, uint8_t row) {
  const float p = duty.percent();
  if (p < 0.0f) {
    console.setRow(row, "AKD duty ...");
  } else {
    console.setRow(row, "AKD duty %.1f%%", p);
  }
}

float corePowerMilliwatts(Board& board) {
  return board.power().milliwatts(Rail::Core0V8);
}

void showPower(Console& console, Board& board, uint8_t row) {
  const float mw = corePowerMilliwatts(board);
  if (isnan(mw)) {
    console.setRow(row, "0V8 n/a");
  } else {
    console.setRow(row, "0V8 %.1f mW", mw);
  }
}

}  // namespace cbb15
