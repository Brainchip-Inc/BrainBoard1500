// BrainBoard1500 example 04 - keyword spotting with the Dev Hub speech model.
//
// Model: BrainChip Dev Hub akida1/model_zoo/speech_commands (DS-CNN,
//        Google Speech Commands, 12 classes: down go left no off on right
//        stop up yes, silence, unknown). Input: 49x10 MFCC as uint8.
// Microphone: Nicla Vision PDM microphone, 16 kHz mono.
//
// Preprocessing on the Nicla Vision (cbb15::Mfcc, identical to the Dev Hub
// training pipeline): DC removal (the PDM microphone has an offset of a few
// hundred counts), peak normalisation of the last second, 30 ms Hann
// windows every 20 ms, 512-point FFT, 40 mel bands (20-4000 Hz), log, DCT,
// 10 coefficients, uint8 scaling with the training range -51.43 .. 14.23.
//
// Every 200 ms the last second of audio is classified. A keyword is reported
// when its average probability over the last four windows is high enough.
// The model was trained on US English recordings: pronounce the words the
// American way.
//
// AKD duty cycle (same definition in every example): share of the elapsed
// time in which the AKD1500 has work in its pipeline,
//   duty = 1 - idle time / elapsed time,  averaged over a window, max. 100 %.
// Sensor capture, preprocessing and the SPI transfers of input and output
// count as idle time.
// Here one job is in flight at a time, so the busy time is the sum of the
// pure inference times measured by the AKD1500 clock counter (2 s windows).

#include <PDM.h>
#include <BrainBoard1500.h>
#include "kws.h"
#include <math.h>

// Where the model comes from:
//   1..4 = BB15 external flash slot (written by 02_ModelFlasher)
//   0    = do not use the flash, push the model into the AKD1500 via SPI
constexpr uint8_t kModelSlot = 2;
// Read the slot header once at start-up to make sure the slot holds this
// model (about 0.5 s through the flash bridge). The flash probe itself is
// always skipped (BoardConfig::assumeFlashReady).
constexpr bool kCheckSlotHeader = true;

constexpr int kSampleRate = 16000;
constexpr int kMicGain = 12;           // 8..16 works; 24 clips
constexpr uint32_t kHopMs = 200;       // analysis interval
constexpr int kHistory = 4;            // windows averaged for a decision
constexpr float kMinProbability = 0.60f;
constexpr float kSilenceRms = 150.0f;  // DC-free RMS below this = silence

// Power of two, so that the unsigned index arithmetic wraps cleanly.
constexpr int kRingSamples = 32768;
constexpr int kSilence = 10;  // label index of "Silence"
constexpr int kUnknown = 11;  // label index of "Unknown"

cbb15::Console console;
cbb15::Board board;
cbb15::Akida runtime;
cbb15::Mfcc mfcc;

int16_t ring[kRingSamples];
volatile uint32_t ringWritten = 0;  // total samples received
int16_t clip[cbb15::Mfcc::kSamples];
uint8_t features[cbb15::Mfcc::kOutputSize];
float history[kHistory][12];
int historyCount = 0;
uint32_t lastHop = 0;

const cbb15::Model& model() {
  static const cbb15::Model m = kModelSlot == 0
                                    ? cbb15::models::kwsWithProgram()
                                    : cbb15::models::kws();
  return m;
}

// PDM callback (interrupt context): append the new samples to the ring.
void onPdmData() {
  static int16_t chunk[512];
  int bytes = PDM.available();
  while (bytes > 0) {
    const int n = bytes > static_cast<int>(sizeof(chunk)) ? sizeof(chunk) : bytes;
    PDM.read(chunk, n);
    const int samples = n / 2;
    uint32_t w = ringWritten;
    for (int i = 0; i < samples; ++i) {
      ring[w % kRingSamples] = chunk[i];
      ++w;
    }
    ringWritten = w;
    bytes -= n;
  }
}

// Copies the newest second of audio and returns its DC-free RMS.
float copyLastSecond() {
  noInterrupts();
  const uint32_t end = ringWritten;
  for (int i = 0; i < cbb15::Mfcc::kSamples; ++i) {
    clip[i] = ring[(end - cbb15::Mfcc::kSamples + i) % kRingSamples];
  }
  interrupts();
  double sum = 0, sq = 0;
  for (int i = 0; i < cbb15::Mfcc::kSamples; ++i) {
    sum += clip[i];
  }
  const double mean = sum / cbb15::Mfcc::kSamples;
  for (int i = 0; i < cbb15::Mfcc::kSamples; ++i) {
    const double v = clip[i] - mean;
    sq += v * v;
  }
  return static_cast<float>(sqrt(sq / cbb15::Mfcc::kSamples));
}

void softmax(const std::vector<float>& in, float* out) {
  float mx = in[0];
  for (float v : in) {
    mx = v > mx ? v : mx;
  }
  float sum = 0;
  for (size_t i = 0; i < in.size(); ++i) {
    out[i] = expf(in[i] - mx);
    sum += out[i];
  }
  for (size_t i = 0; i < in.size(); ++i) {
    out[i] /= sum;
  }
}

void setup() {
  cbb15::startBoard(console, board, runtime, "BB15 keywords");
  cbb15::loadModel(console, runtime, model(), kModelSlot, kCheckSlotHeader);
  if (!mfcc.begin()) {
    cbb15::halt(console, "MFCC tables", cbb15::Status::InvalidArgument);
  }
  PDM.onReceive(onPdmData);
  PDM.setGain(kMicGain);
  if (!PDM.begin(1, kSampleRate)) {
    cbb15::halt(console, "PDM microphone", cbb15::Status::NotStarted);
  }
  console.logf("say: yes no up down");
  console.logf("left right on off");
  console.logf("stop go");
  delay(1500);  // fill the audio ring
  console.clearRows();
}

void loop() {
  if (millis() - lastHop < kHopMs) {
    return;
  }
  lastHop = millis();

  const float rms = copyLastSecond();
  float probs[12] = {};
  float latencyMs = 0;
  float computeMs = 0;
  static cbb15::DutyCycle duty;
  if (rms < kSilenceRms) {
    probs[kSilence] = 1.0f;  // skip the AKD1500 while it is quiet
  } else {
    mfcc.compute(clip, cbb15::Mfcc::kSamples, features, true);
    cbb15::InferenceResult r;
    const cbb15::Status st = runtime.infer(features, r);
    if (st != cbb15::Status::Ok) {
      console.logf("infer: %s", cbb15::statusName(st));
      return;
    }
    softmax(r.scores, probs);
    latencyMs = r.latencyUs / 1000.0f;
    computeMs = r.computeUs / 1000.0f;
    duty.add(r.computeUs);
  }

  // Sliding window of the last kHistory results.
  memmove(history[1], history[0], sizeof(history[0]) * (kHistory - 1));
  memcpy(history[0], probs, sizeof(probs));
  historyCount = historyCount < kHistory ? historyCount + 1 : kHistory;
  float mean[12] = {};
  for (int h = 0; h < historyCount; ++h) {
    for (int c = 0; c < 12; ++c) {
      mean[c] += history[h][c] / historyCount;
    }
  }
  int best = 0;
  for (int c = 1; c < 12; ++c) {
    best = mean[c] > mean[best] ? c : best;
  }
  const bool keyword =
      best != kSilence && best != kUnknown && mean[best] >= kMinProbability;
  static uint32_t shownAt = 0;
  static int shown = -1;
  if (keyword && best != shown) {
    shown = best;
    shownAt = millis();
    cbb15::setLed(cbb15::LedColor::Green);
  } else if (shown >= 0 && millis() - shownAt > 1500) {
    shown = -1;
    cbb15::setLed(cbb15::LedColor::Off);
  }

  console.setRow(1, "word: %s", shown >= 0 ? model().label(shown) : "-");
  console.setRow(2, "top %s %.2f", model().label(best), mean[best]);
  console.setRow(3, "rms %.0f", rms);
  // Pure AKD1500 inference time, and the time including the SPI transfer.
  console.setRow(4, "AKD %.2fms tot %.1fms", computeMs, latencyMs);
  cbb15::showPower(console, board, 5);
  // Silent windows skip the AKD1500 and count as idle time.
  cbb15::showDuty(console, duty, 6);
  console.publish();
}
