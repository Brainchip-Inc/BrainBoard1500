// BrainBoard1500 example 06 - ECG heartbeat classification.
//
// Model: BrainChip Dev Hub akida1/model_zoo/arrhythmia_classification,
//        36x32x1 input (Morlet wavelet scalogram of one heartbeat plus four
//        rows of RR-interval features), classes N (normal),
//        S (supraventricular ectopic), V (ventricular ectopic).
// Data:  30 beats of the MIT-BIH test records (DS2, patients never seen in
//        training), preprocessed on the PC by the Dev Hub pipeline
//        (tools/make_samples.py). The Nicla Vision has no ECG front end, so
//        this example replays these beats.
//
// The model is small (63 KB), so it is pushed into the AKD1500 over SPI
// (slot 0) and does not need a flash slot. For every beat the board result
// is compared with the dataset label and with the akida Python simulation.
//
// AKD duty cycle (same definition in every example): share of the elapsed
// time in which the AKD1500 has work in its pipeline,
//   duty = 1 - idle time / elapsed time,  averaged over a window, max. 100 %.
// Sensor capture, preprocessing and the SPI transfers of input and output
// count as idle time.
// Here one job is in flight at a time, so the busy time is the sum of the
// pure inference times measured by the AKD1500 clock counter (2 s windows).

#include <BrainBoard1500.h>
#include "ecg.h"
#include "samples_ecg.h"

// Where the model comes from:
//   1..4 = BB15 external flash slot (written by 02_ModelFlasher)
//   0    = do not use the flash, push the model into the AKD1500 via SPI
constexpr uint8_t kModelSlot = 0;

// Pause between beats so that the power monitor (38 ms update) and the
// display can follow.
constexpr uint32_t kBeatIntervalMs = 400;

cbb15::Console console;
cbb15::Board board;
cbb15::Akida runtime;

const cbb15::Model& model() {
  static const cbb15::Model m = kModelSlot == 0
                                    ? cbb15::models::ecgWithProgram()
                                    : cbb15::models::ecg();
  return m;
}

void setup() {
  cbb15::startBoard(console, board, runtime, "BB15 ECG beats");
  cbb15::loadModel(console, runtime, model(), kModelSlot);
  delay(1000);
  console.clearRows();
}

void loop() {
  using namespace cbb15::samples;
  static uint16_t index = 0;
  static uint32_t total = 0, correct = 0, agree = 0;
  static cbb15::DutyCycle duty;

  cbb15::InferenceResult r;
  const cbb15::Status st = runtime.infer(samples_ecg_data[index], r);
  if (st != cbb15::Status::Ok) {
    console.logf("infer: %s", cbb15::statusName(st));
    delay(kBeatIntervalMs);
    return;
  }
  const char* predicted = model().label(r.best);
  const bool isCorrect = predicted[0] == samples_ecg_truth[index][0];
  ++total;
  correct += isCorrect;
  agree += r.best == samples_ecg_expected[index];
  cbb15::setLed(r.best == 0 ? cbb15::LedColor::Green : cbb15::LedColor::Red);

  console.setRow(1, "beat %u truth %s", index, samples_ecg_truth[index]);
  console.setRow(2, "-> %.18s", predicted);
  console.setRow(3, "acc %lu/%lu", static_cast<unsigned long>(correct),
                 static_cast<unsigned long>(total));
  console.setRow(4, "same as PC %lu/%lu", static_cast<unsigned long>(agree),
                 static_cast<unsigned long>(total));
  duty.add(r.computeUs);
  // Pure AKD1500 inference time, and the time including the SPI transfer.
  console.setRow(5, "AKD %.2fms tot %.1fms", r.computeUs / 1000.0f,
                 r.latencyUs / 1000.0f);
  cbb15::showPower(console, board, 6);
  // One beat every kBeatIntervalMs: the AKD1500 is idle most of the time.
  cbb15::showDuty(console, duty, 7);
  console.publish(true);

  index = (index + 1) % samples_ecg_count;
  delay(kBeatIntervalMs);
}
