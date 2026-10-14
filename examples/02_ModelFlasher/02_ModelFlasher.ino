// BrainBoard1500 example 02 - write models into the four BB15 flash slots.
//
// The BB15 rev 2 flash (Renesas AT25SL321, 4 MiB) is split into four equal
// slots of 1 MiB:
//   1 = BB15 external flash, slot 1 at offset 0x000000
//   2 = BB15 external flash, slot 2 at offset 0x100000 (1 MiB)
//   3 = BB15 external flash, slot 3 at offset 0x200000 (2 MiB)
//   4 = BB15 external flash, slot 4 at offset 0x300000 (3 MiB)
//   0 = no flash: a model is pushed into the AKD1500 over SPI at run time
//
// This sketch probes the flash once (JEDEC id, quad enable), writes every
// model of kJobs into its slot, verifies it byte by byte, and finally boots
// each slot into the AKD1500 and runs a short self test. A slot that
// already holds exactly the same model is not written again.
//
// The default assignment matches the other examples:
//   slot 1  vww         03_PersonDetection
//   slot 2  kws         04_KeywordSpotting
//   slot 3  imagenet    05_ImageNetClassification
//   slot 4  face_learn  07_FaceLearning
// 06_EcgArrhythmia and 08_VoiceLearning push their small models over SPI.

#include <BrainBoard1500.h>
#include "face_learn.h"
#include "imagenet.h"
#include "kws.h"
#include "samples_imagenet.h"
#include "samples_vww.h"
#include "vww.h"

#include <string.h>

#include <vector>

cbb15::Console console;
cbb15::Board board;
cbb15::Akida runtime;

// Set to true to rewrite slots even if they already hold the same model.
constexpr bool kForceWrite = false;

struct Job {
  uint8_t slot;
  cbb15::Model model;  // must include the program (...WithProgram())
};

Job kJobs[] = {
    {1, cbb15::models::vwwWithProgram()},
    {2, cbb15::models::kwsWithProgram()},
    {3, cbb15::models::imagenetWithProgram()},
    {4, cbb15::models::faceLearnWithProgram()},
};

// Runs the sample inputs of a model (if any) and compares the result with
// the akida Python simulation.
void selfTest(const Job& job) {
  using namespace cbb15::samples;
  const uint8_t* samples[2] = {nullptr, nullptr};
  const int16_t* expected = nullptr;
  uint16_t count = 0;
  if (strcmp(job.model.name, "vww") == 0) {
    for (uint16_t i = 0; i < samples_vww_count && i < 2; ++i) {
      samples[i] = samples_vww_data[i];
    }
    expected = samples_vww_expected;
    count = samples_vww_count < 2 ? samples_vww_count : 2;
  } else if (strcmp(job.model.name, "imagenet") == 0) {
    for (uint16_t i = 0; i < samples_imagenet_count && i < 2; ++i) {
      samples[i] = samples_imagenet_data[i];
    }
    expected = samples_imagenet_expected;
    count = samples_imagenet_count < 2 ? samples_imagenet_count : 2;
  }
  if (count == 0) {
    // No sample: one inference on a neutral grey input proves the boot.
    std::vector<uint8_t> grey(job.model.inputSize(), 128);
    cbb15::InferenceResult r;
    const cbb15::Status st = runtime.infer(grey.data(), r);
    console.logf("  run: %s, %lu us", cbb15::statusName(st),
                 static_cast<unsigned long>(r.latencyUs));
    return;
  }
  for (uint16_t i = 0; i < count; ++i) {
    cbb15::InferenceResult r;
    const cbb15::Status st = runtime.infer(samples[i], r);
    if (st != cbb15::Status::Ok) {
      console.logf("  sample %u: %s", i, cbb15::statusName(st));
      continue;
    }
    console.logf("  sample %u: %s", i, r.best == expected[i] ? "PASS" : "FAIL");
    console.logf("  -> %.20s", job.model.label(r.best));
  }
}

void setup() {
  // Full flash probe: makes sure the quad enable bit is set, which the
  // other examples rely on when they skip the probe.
  cbb15::BoardConfig config;
  config.assumeFlashReady = false;
  cbb15::startBoard(console, board, runtime, "BB15 model flasher", config);

  for (const Job& job : kJobs) {
    const cbb15::Status st =
        board.writeModelToSlot(job.slot, job.model, &console, kForceWrite);
    if (st != cbb15::Status::Ok) {
      cbb15::halt(console, job.model.name, st);
    }
  }
  console.logf("all slots written");

  // Boot every slot into the AKD1500 (flash path) and test it.
  for (const Job& job : kJobs) {
    cbb15::loadModel(console, runtime, job.model, job.slot, true);
    selfTest(job);
  }
  console.logf("done, %.1f mW on 0V8", cbb15::corePowerMilliwatts(board));
  cbb15::setLed(cbb15::LedColor::Green);
}

void loop() {}
