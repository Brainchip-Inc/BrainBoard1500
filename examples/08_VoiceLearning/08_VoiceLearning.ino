// BrainBoard1500 example 08 - learn your voice on the AKD1500 (on-chip learning).
//
// Model: voice_learn (tools/make_learning_models.py): an Akida learning
//        layer with 2 classes x 10 neurons on a 304-bit speaker code.
//          class 0 = my voice, class 1 = other voice
// The speaker code is computed on the Nicla Vision (cbb15::SpeakerEncoder):
// spectral envelope of the voiced frames (32 mel bands, independent of the
// loudness) plus the median pitch. Learning happens on the AKD1500 itself,
// one utterance per step. A keyword network is not used here because it is
// trained to ignore who is speaking.
//
// LEDs after every utterance: green = your voice, blue = another voice.
//
// Commands (serial monitor, 115200 baud, send one letter):
//   m  the NEXT utterance is learned as MY voice
//   o  the NEXT utterance is learned as an OTHER voice
//   s  save the learned weights to the BB15 flash (user area of a slot)
//   l  load the learned weights from the flash
//   f  forget everything (reprogram the model)
// Teach each class with several sentences or words (one neuron per
// utterance, up to 10 per class). Learned weights are restored at start-up.
//
// AKD duty cycle (same definition in every example): share of the elapsed
// time in which the AKD1500 has work in its pipeline,
//   duty = 1 - idle time / elapsed time,  averaged over a window, max. 100 %.
// Sensor capture, preprocessing and the SPI transfers of input and output
// count as idle time.
// Not shown here: the AKD1500 runs once per utterance, so the duty cycle
// would stay near 0 %. The OLED shows the pure inference time instead.

#include <PDM.h>
#include <BrainBoard1500.h>
#include "voice_learn.h"
#include <math.h>

#include <algorithm>
#include <vector>

// Where the model comes from:
//   1..4 = BB15 external flash slot (written by 02_ModelFlasher)
//   0    = do not use the flash, push the model into the AKD1500 via SPI
// The model is only 2.3 KB, so it is pushed over SPI by default.
constexpr uint8_t kModelSlot = 0;
// Flash slot whose 64 KiB user area stores the learned weights.
constexpr uint8_t kStoreSlot = 2;

constexpr int kSampleRate = 16000;
constexpr int kMicGain = 12;
constexpr int kBlock = 320;               // 20 ms analysis block
constexpr float kOnsetFactor = 4.0f;      // loudness over the noise floor
constexpr float kMinOnsetRms = 200.0f;
constexpr uint32_t kPreRollMs = 200;      // audio kept before the onset
constexpr uint32_t kEndSilenceMs = 350;   // quiet time that ends an utterance
constexpr uint32_t kMaxUtteranceMs = 1800;
constexpr uint32_t kMinUtteranceMs = 250;
constexpr int kClasses = 2;
constexpr int kNeuronsPerClass = 10;

constexpr int kRingSamples = 65536;  // power of two, about 4 s
const char* const kClassName[kClasses] = {"MY VOICE", "OTHER"};

cbb15::Console console;
cbb15::Board board;
cbb15::Akida runtime;
cbb15::SpeakerEncoder encoder;

int16_t ring[kRingSamples];
volatile uint32_t ringWritten = 0;
int16_t utterance[32000];
uint8_t code[cbb15::SpeakerEncoder::kBits];

uint32_t learnedMask = 0;
int pendingLabel = -1;  // set by the m / o commands
float noiseFloor = 100.0f;

const cbb15::Model& model() {
  static const cbb15::Model m = kModelSlot == 0
                                    ? cbb15::models::voiceLearnWithProgram()
                                    : cbb15::models::voiceLearn();
  return m;
}

void onPdmData() {
  static int16_t chunk[512];
  int bytes = PDM.available();
  while (bytes > 0) {
    const int n = bytes > static_cast<int>(sizeof(chunk)) ? sizeof(chunk) : bytes;
    PDM.read(chunk, n);
    uint32_t w = ringWritten;
    for (int i = 0; i < n / 2; ++i) {
      ring[w % kRingSamples] = chunk[i];
      ++w;
    }
    ringWritten = w;
    bytes -= n;
  }
}

// DC-free RMS of the block that ends at sample index `end`.
float blockRms(uint32_t end) {
  float mean = 0;
  for (int i = 0; i < kBlock; ++i) {
    mean += ring[(end - kBlock + i) % kRingSamples];
  }
  mean /= kBlock;
  float sq = 0;
  for (int i = 0; i < kBlock; ++i) {
    const float v = ring[(end - kBlock + i) % kRingSamples] - mean;
    sq += v * v;
  }
  return sqrtf(sq / kBlock);
}

float classScore(const cbb15::InferenceResult& r, int c) {
  float best = -1e30f;
  for (int n = 0; n < kNeuronsPerClass; ++n) {
    best = std::max(best, r.scores[c * kNeuronsPerClass + n]);
  }
  return best;
}

void save() {
  std::vector<uint32_t> words;
  cbb15::Status st = runtime.readLearnMemory(words);
  if (st == cbb15::Status::Ok) {
    words.push_back(learnedMask);
    st = board.writeUserData(kStoreSlot, words.data(), words.size() * 4,
                             model().infoCrc32);
  }
  console.logf("save: %s", cbb15::statusName(st));
}

void load() {
  std::vector<uint32_t> words(runtime.learnMemoryWords() + 1);
  uint32_t size = 0;
  cbb15::Status st = board.readUserData(kStoreSlot, words.data(),
                                        words.size() * 4, size,
                                        model().infoCrc32);
  if (st == cbb15::Status::Ok && size == words.size() * 4) {
    learnedMask = words.back();
    words.pop_back();
    st = runtime.writeLearnMemory(words);
  } else if (st == cbb15::Status::Ok) {
    st = cbb15::Status::StoreEmpty;
  }
  console.logf("load: %s", cbb15::statusName(st));
}

void command(char c) {
  switch (c) {
    case 'm': pendingLabel = 0; console.logf("speak: MY voice"); break;
    case 'o': pendingLabel = 1; console.logf("speak: OTHER voice"); break;
    case 's': save(); break;
    case 'l': load(); break;
    case 'f':
      runtime.forget();
      learnedMask = 0;
      console.logf("forgotten");
      break;
    default: break;
  }
}

void handleUtterance(uint32_t start, uint32_t end) {
  uint32_t count = end - start;
  if (count > sizeof(utterance) / sizeof(utterance[0])) {
    count = sizeof(utterance) / sizeof(utterance[0]);
  }
  for (uint32_t i = 0; i < count; ++i) {
    utterance[i] = ring[(start + i) % kRingSamples];
  }
  if (!encoder.encode(utterance, count, code)) {
    return;
  }
  cbb15::InferenceResult r;
  if (pendingLabel >= 0) {
    const cbb15::Status st = runtime.learn(code, pendingLabel, &r);
    console.logf("learned %s: %s", kClassName[pendingLabel],
                 cbb15::statusName(st));
    learnedMask |= 1u << pendingLabel;
    pendingLabel = -1;
    return;
  }
  const cbb15::Status st = runtime.infer(code, r);
  if (st != cbb15::Status::Ok) {
    console.logf("infer: %s", cbb15::statusName(st));
    return;
  }
  const float me = classScore(r, 0);
  const float other = classScore(r, 1);
  const bool ready = learnedMask == 0x3;
  const int decided = me >= other ? 0 : 1;
  if (ready) {
    cbb15::setLed(decided == 0 ? cbb15::LedColor::Green : cbb15::LedColor::Blue);
  }
  console.setRow(1, "%s", ready ? kClassName[decided] : "teach m and o");
  console.setRow(2, "me %.0f other %.0f", me, other);
  console.setRow(3, "pitch %.0f Hz", encoder.pitchHz());
  console.setRow(4, "%lu ms speech", static_cast<unsigned long>(count * 1000 / kSampleRate));
  // Pure AKD1500 inference time, and the time including the SPI transfer.
  // No duty cycle here: the AKD1500 only runs once per utterance.
  console.setRow(5, "AKD %.2fms tot %.1fms", r.computeUs / 1000.0f,
                 r.latencyUs / 1000.0f);
  cbb15::showPower(console, board, 6);
  console.publish(true);
}

void setup() {
  cbb15::startBoard(console, board, runtime, "BB15 voice learning");
  cbb15::loadModel(console, runtime, model(), kModelSlot);
  if (!encoder.begin(cbb15::models::voice_learn_band_centre)) {
    cbb15::halt(console, "encoder tables", cbb15::Status::InvalidArgument);
  }
  load();
  PDM.onReceive(onPdmData);
  PDM.setGain(kMicGain);
  if (!PDM.begin(1, kSampleRate)) {
    cbb15::halt(console, "PDM microphone", cbb15::Status::NotStarted);
  }
  console.logf("m=me o=other s=save");
  delay(1500);
}

void loop() {
  while (Serial.available() > 0) {
    command(static_cast<char>(Serial.read()));
  }
  static uint32_t analysed = 0;     // ring index of the last analysed block
  static uint32_t onset = 0;        // ring index where speech started
  static uint32_t lastLoud = 0;     // ring index of the last loud block
  static bool speaking = false;
  static uint32_t ledOffAt = 0;

  const uint32_t written = ringWritten;
  if (analysed == 0) {
    analysed = written;
  }
  while (written - analysed >= static_cast<uint32_t>(kBlock)) {
    analysed += kBlock;
    const float rms = blockRms(analysed);
    const float onsetLevel = std::max(kMinOnsetRms, kOnsetFactor * noiseFloor);
    const bool loud = rms > onsetLevel;
    if (!speaking) {
      // Track the noise floor slowly while nobody speaks.
      noiseFloor = 0.98f * noiseFloor + 0.02f * rms;
      if (loud) {
        speaking = true;
        onset = analysed - kBlock;
        lastLoud = analysed;
      }
      continue;
    }
    if (loud) {
      lastLoud = analysed;
    }
    const uint32_t lengthMs = (analysed - onset) * 1000 / kSampleRate;
    const uint32_t quietMs = (analysed - lastLoud) * 1000 / kSampleRate;
    if (quietMs >= kEndSilenceMs || lengthMs >= kMaxUtteranceMs) {
      speaking = false;
      const uint32_t start = onset - kPreRollMs * kSampleRate / 1000;
      if ((lastLoud - onset) * 1000 / kSampleRate >= kMinUtteranceMs) {
        // Keep 100 ms after the last loud block, but never read past the
        // samples analysed so far.
        const uint32_t tail = lastLoud + kBlock * 5;
        handleUtterance(start, (analysed - tail) < 0x80000000u ? tail : analysed);
        ledOffAt = millis() + 2000;
      }
    }
  }
  if (ledOffAt != 0 && static_cast<int32_t>(millis() - ledOffAt) > 0) {
    cbb15::setLed(cbb15::LedColor::Off);
    ledOffAt = 0;
  }
  static uint32_t lastIdle = 0;
  if (!speaking && millis() - lastIdle > 2000) {
    lastIdle = millis();
    console.setRow(7, "noise %.0f %s", noiseFloor,
                   pendingLabel == 0 ? "[learn me]" : pendingLabel == 1 ? "[learn other]" : "");
    cbb15::showPower(console, board, 6);
    console.publish(true);
  }
}
