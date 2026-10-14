// BrainBoard1500 example 11 - AKD1500 power in five operating states.
//
// Endless loop, kStateMs (10 s) in every state:
//   FULL   100 % duty cycle: input resident in AKD1500 SRAM, 15-job pipeline
//          kept full (as in 09_MaxDutyCycle)
//   DUTY10  10 % duty cycle: one job from the resident input, then a pause of
//          nine times its duration
//   IDLE   model loaded, nothing queued
//   SLEEP  AKD_SLEEP high (expander P3): clocks gated, rails and SRAM kept
//   OFF    BB15 power enable (A8) low: 0.8 V and 1.8 V rails off = 0 mW
//
// Duty cycle has the same meaning as in the other examples: the share of the
// time in which the AKD1500 has work queued. Input: a fixed Dev Hub sample
// image, so FULL and DUTY10 are the same load in every round.
//
// Measurement: the INA2227 power of the 0.8 V rail is averaged in every state
// after a settling time of kSettleMs (the INA2227 averages 16 conversions,
// ~38 ms per channel pair). In OFF the rails are down and the INA2227 is
// disabled with them, so OFF is shown as 0 mW. After OFF the complete
// bring-up runs again (power sequencer, expander, SPI link test, INA2227
// configuration, model from flash slot 1, input re-staged); the time it takes
// is shown as "wake".
//
// After SLEEP the model and the resident input are still in the AKD1500
// SRAM; one test job checks that, and a full restart follows if it fails.
//
// LED: red = FULL, yellow = DUTY10, green = IDLE, blue = SLEEP, white = OFF.
//
// Output on Serial and OLED: current state, remaining time, live power, duty
// cycle, and the averages of the last round. One summary line per round on
// Serial.

#include <BrainBoard1500.h>
#include "samples_vww.h"
#include "vww.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

// Where the model comes from:
//   1..4 = BB15 external flash slot (written by 02_ModelFlasher)
//   0    = do not use the flash, push the model into the AKD1500 via SPI
constexpr uint8_t kModelSlot = 1;

constexpr uint32_t kStateMs = 10000;
constexpr uint32_t kSettleMs = 1000;   // not part of the average
constexpr uint32_t kSampleMs = 100;    // INA2227 sampling interval
constexpr uint32_t kDisplayMs = 500;
constexpr float kLowDuty = 0.10f;
constexpr uint8_t kPipelineDepth = 15;

enum State : uint8_t { kFull = 0, kDuty10, kIdle, kSleep, kOff, kStateCount };
const char* const kStateName[kStateCount] = {"FULL 100%", "DUTY 10%", "IDLE",
                                             "SLEEP", "OFF"};
// Nicla Vision RGB LED colour per state. The LED is powered by the Nicla,
// not by the measured AKD1500 0.8 V rail, so it does not affect the reading.
const cbb15::LedColor kStateLed[kStateCount] = {
    cbb15::LedColor::Red,     // FULL 100 %
    cbb15::LedColor::Yellow,  // DUTY 10 %
    cbb15::LedColor::Green,   // IDLE
    cbb15::LedColor::Blue,    // SLEEP
    cbb15::LedColor::White,   // OFF
};
const char* const kShortName[kStateCount] = {"F100", "D10", "IDLE", "SLP",
                                             "OFF"};

cbb15::Console console;
cbb15::Board board;
cbb15::Akida runtime;
uint8_t input[96 * 96 * 3];

State state = kFull;
uint32_t stateStart = 0;
float average[kStateCount] = {NAN, NAN, NAN, NAN, NAN};
double sum = 0;
uint32_t samples = 0;
uint32_t lastSample = 0;
uint32_t lastDisplay = 0;
uint32_t wakeMs = 0;
// Duty cycle bookkeeping of the current state.
uint64_t busyUs = 0;
uint32_t dutyStart = 0;
uint32_t nextJobUs = 0;

const cbb15::Model& model() {
  static const cbb15::Model m = kModelSlot == 0
                                    ? cbb15::models::vwwWithProgram()
                                    : cbb15::models::vww();
  return m;
}

// Model into the AKD1500, input into its SRAM, pipeline configured.
void startRuntime(bool checkSlot) {
  cbb15::Status st = runtime.begin(board);
  if (st == cbb15::Status::Ok) {
    st = runtime.load(model(), kModelSlot, checkSlot, &console);
  }
  if (st == cbb15::Status::Ok) {
    st = runtime.startStream(input, kPipelineDepth);
  }
  if (st != cbb15::Status::Ok) {
    cbb15::halt(console, "runtime", st);
  }
}

// Complete bring-up after OFF (or after a failed wake from SLEEP).
void restart() {
  const uint32_t t0 = millis();
  runtime.end();
  const cbb15::Status st = board.begin(cbb15::BoardConfig(), nullptr);
  if (st != cbb15::Status::Ok) {
    cbb15::halt(console, "BB15 restart", st);
  }
  // The slot was checked at start-up; the flash content survives OFF.
  startRuntime(false);
  wakeMs = millis() - t0;
}

void enterState(State next) {
  // Leave the current state.
  if (state == kSleep) {
    board.wake();
    if (runtime.streamOne() != cbb15::Status::Ok) {
      console.logf("wake test failed, restart");
      restart();
    }
  } else if (state == kOff) {
    restart();
  }

  state = next;
  cbb15::setLed(kStateLed[next]);
  stateStart = millis();
  sum = 0;
  samples = 0;
  busyUs = 0;
  dutyStart = millis();
  nextJobUs = micros();
  runtime.takeStreamIdleUs();  // start a fresh idle measurement

  // Enter the new state. The pipeline is empty after streamOne(); IDLE,
  // SLEEP and OFF must start with no job queued.
  if (next == kDuty10 || next == kIdle || next == kSleep || next == kOff) {
    runtime.streamOne();  // drains the pipeline (one short job)
  }
  if (next == kSleep) {
    board.sleep();
  } else if (next == kOff) {
    runtime.end();
    board.powerOff();
  }
}

// FULL: share of the last display interval with work queued (measured idle
// time of the pipeline). DUTY10: sum of the job times since the state began.
float dutyPercent() {
  const uint32_t now = millis();
  const uint32_t elapsed = now - dutyStart;
  float duty = 0.0f;
  if (state == kFull && elapsed > 0) {
    const float idleMs = runtime.takeStreamIdleUs() / 1000.0f;
    duty = 100.0f * (1.0f - idleMs / elapsed);
    dutyStart = now;
  } else if (state == kDuty10 && elapsed > 0) {
    duty = busyUs / (elapsed * 10.0f);  // us / ms -> %
  }
  return duty < 0.0f ? 0.0f : (duty > 100.0f ? 100.0f : duty);
}

// "123.4" or "  -  " before the first measurement of a state.
const char* mw(float v, char* buf) {
  if (isnan(v)) {
    strcpy(buf, "  -  ");
  } else {
    snprintf(buf, 8, "%5.1f", v);
  }
  return buf;
}

void work() {
  if (state == kFull) {
    uint32_t done = 0;
    runtime.streamStep(done);
  } else if (state == kDuty10) {
    if (static_cast<int32_t>(micros() - nextJobUs) >= 0) {
      const uint32_t t0 = micros();
      uint32_t busy = 0;
      if (runtime.streamOne(&busy) == cbb15::Status::Ok) {
        busyUs += busy;
        // Next job after a pause of (1 / duty - 1) times the busy time.
        nextJobUs = t0 + static_cast<uint32_t>(busy / kLowDuty);
      }
    }
  }
}

float readPower() {
  if (state == kOff) {
    return 0.0f;  // rails are off; the INA2227 is disabled with them
  }
  return cbb15::corePowerMilliwatts(board);
}

void showSummary() {
  char line[96];
  snprintf(line, sizeof(line),
           "round: F100 %.1f | D10 %.1f | IDLE %.1f | SLEEP %.1f | OFF %.1f mW | wake %lu ms",
           average[kFull], average[kDuty10], average[kIdle], average[kSleep],
           average[kOff], static_cast<unsigned long>(wakeMs));
  Serial.println(line);
}

void setup() {
  cbb15::startBoard(console, board, runtime, "BB15 power modes");
  memcpy(input, cbb15::samples::samples_vww_data[0], sizeof(input));
  console.logf("load %s from slot %u", model().name, kModelSlot);
  startRuntime(true);
  console.logf("states of %lu s each", static_cast<unsigned long>(kStateMs / 1000));
  delay(1000);
  console.clearRows();
  enterState(kFull);
}

void loop() {
  work();

  const uint32_t now = millis();
  const uint32_t inState = now - stateStart;
  static float live = NAN;
  if (now - lastSample >= kSampleMs) {
    lastSample = now;
    live = readPower();
    if (inState >= kSettleMs && !isnan(live)) {
      sum += live;
      ++samples;
    }
  }

  if (now - lastDisplay >= kDisplayMs) {
    lastDisplay = now;
    const float duty = dutyPercent();
    console.setRow(1, "%s  %lus", kStateName[state],
                   static_cast<unsigned long>((kStateMs - std::min(inState, kStateMs) + 999) / 1000));
    console.setRow(2, "0V8 %.1f mW", isnan(live) ? 0.0f : live);
    console.setRow(3, "AKD duty %.1f%%", duty);
    char a[8], b[8];
    console.setRow(4, "F100 %s D10 %s", mw(average[kFull], a),
                   mw(average[kDuty10], b));
    console.setRow(5, "IDLE %s SLP %s", mw(average[kIdle], a),
                   mw(average[kSleep], b));
    console.setRow(6, "OFF %s wake %lums", mw(average[kOff], a),
                   static_cast<unsigned long>(wakeMs));
    console.setRow(7, "averages in mW");
    console.publish(true);
  }

  if (inState >= kStateMs) {
    if (samples > 0) {
      average[state] = static_cast<float>(sum / samples);
    }
    if (state == kOff) {
      showSummary();
    }
    enterState(static_cast<State>((state + 1) % kStateCount));
  }
}
