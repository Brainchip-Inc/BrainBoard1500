// BrainBoard1500 example 07 - learn your face on the AKD1500 (on-chip learning).
//
// Model: face_learn (tools/make_learning_models.py). Feature extractor is
//        the Dev Hub VWW AkidaNet (96x96x3) whose last feature layer was
//        turned into 256 binary features; on top sits an Akida learning
//        layer with 3 classes x 10 neurons:
//          class 0 = me, class 1 = other person, class 2 = background
// Learning happens on the AKD1500 itself, one camera frame per step.
//
// LEDs: green = it sees you, blue = it sees someone else, off = background
//       or not sure. Hold the Nicla Vision with the micro-USB connector
//       pointing down (the image is rotated by 180 degrees).
//
// Commands (serial monitor, 115200 baud, send one letter):
//   m  learn ME            - look into the camera, 5 views are taken
//   o  learn OTHER person  - point it at someone else
//   b  learn BACKGROUND    - point it at the room without a face
//   s  save the learned weights to the BB15 flash (user area of a slot)
//   l  load the learned weights from the flash
//   f  forget everything (reprogram the model)
// Teach each class a few times from slightly different angles and
// distances: every learning step stores one view in one neuron (up to 10
// per class). Learned weights are restored from the flash at start-up.
//
// AKD duty cycle (same definition in every example): share of the elapsed
// time in which the AKD1500 has work in its pipeline,
//   duty = 1 - idle time / elapsed time,  averaged over a window, max. 100 %.
// Sensor capture, preprocessing and the SPI transfers of input and output
// count as idle time.
// Here one job is in flight at a time, so the busy time is the sum of the
// pure inference times measured by the AKD1500 clock counter (2 s windows).

#include <BrainBoard1500.h>
#include "face_learn.h"

#include <algorithm>
#include <vector>

#include "camera.h"
#include "gc2145.h"

// Where the model comes from:
//   1..4 = BB15 external flash slot (written by 02_ModelFlasher)
//   0    = do not use the flash, push the model into the AKD1500 via SPI
constexpr uint8_t kModelSlot = 4;
// Read the slot header once at start-up to make sure the slot holds this
// model (about 0.5 s through the flash bridge). The flash probe itself is
// always skipped (BoardConfig::assumeFlashReady).
constexpr bool kCheckSlotHeader = true;
// Flash slot whose 64 KiB user area stores the learned weights.
constexpr uint8_t kStoreSlot = 4;

constexpr uint8_t kCameraResolution = CAMERA_R320x240;
constexpr uint8_t kCameraImageMode = CAMERA_RGB565;
// Has no effect: GC2145::setFrameRate() is empty in the Arduino core, the
// sensor runs at a fixed 21.6 frames/s at 320x240 (measured).
constexpr int kCameraFrameRate = 15;
constexpr size_t kFrameBytes = 320 * 240 * 2;

constexpr uint16_t kInputSize = 96;
constexpr int kClasses = 3;
constexpr int kNeuronsPerClass = 10;
constexpr int kViewsPerCommand = 5;
constexpr uint32_t kViewIntervalMs = 300;
constexpr int kVoteWindow = 5;
constexpr int kVotesNeeded = 3;

enum Class { kMe = 0, kOther = 1, kBackground = 2 };
const char* const kClassName[kClasses] = {"ME", "OTHER", "background"};

cbb15::Console console;
cbb15::Board board;
cbb15::Akida runtime;
GC2145 galaxyCore;
Camera cam(galaxyCore);
// Double-buffered capture (cbb15::CameraStream): the next frame is read
// out while the current one is processed. Both frames in AXI SRAM, where
// the DMA can reach them.
cbb15::CameraStream stream;
alignas(32) uint8_t frames[2][kFrameBytes];
uint8_t input[kInputSize * kInputSize * 3];

uint32_t learnedMask = 0;  // bit c set once class c has been taught
int votes[kVoteWindow];
int voteCount = 0;

const cbb15::Model& model() {
  static const cbb15::Model m = kModelSlot == 0
                                    ? cbb15::models::faceLearnWithProgram()
                                    : cbb15::models::faceLearn();
  return m;
}

bool grab() {
  const uint8_t* pixels = stream.next(3000);
  if (pixels == nullptr) {
    return false;
  }
  // Faces are usually in the middle: use the central 240x240 square.
  cbb15::FrameFormat format;
  cbb15::rgb565ToRgb(pixels, format, cbb15::centreSquare(format),
                     input, kInputSize, kInputSize);
  return true;
}

// Class score = strongest neuron of the class (neurons are laid out in
// contiguous blocks of kNeuronsPerClass).
float classScore(const cbb15::InferenceResult& r, int c) {
  float best = -1e30f;
  for (int n = 0; n < kNeuronsPerClass; ++n) {
    best = std::max(best, r.scores[c * kNeuronsPerClass + n]);
  }
  return best;
}

void learnClass(int c) {
  for (int v = 0; v < kViewsPerCommand; ++v) {
    console.logf("learn %s %d/%d", kClassName[c], v + 1, kViewsPerCommand);
    cbb15::setLed(cbb15::LedColor::Yellow);
    // After the pause between two views the waiting frame is old: take a
    // new one, so every learned view shows the face as it is now.
    stream.skipFinished();
    if (!grab()) {
      console.logf("frame timeout");
      continue;
    }
    const cbb15::Status st = runtime.learn(input, c);
    if (st != cbb15::Status::Ok) {
      cbb15::halt(console, "learn", st);
    }
    cbb15::setLed(cbb15::LedColor::Off);
    delay(kViewIntervalMs);
  }
  learnedMask |= 1u << c;
  voteCount = 0;
}

void save() {
  std::vector<uint32_t> words;
  cbb15::Status st = runtime.readLearnMemory(words);
  if (st == cbb15::Status::Ok) {
    words.push_back(learnedMask);  // the class mask travels with the weights
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
  voteCount = 0;
}

void command(char c) {
  switch (c) {
    case 'm': learnClass(kMe); break;
    case 'o': learnClass(kOther); break;
    case 'b': learnClass(kBackground); break;
    case 's': save(); break;
    case 'l': load(); break;
    case 'f':
      runtime.forget();
      learnedMask = 0;
      voteCount = 0;
      console.logf("forgotten");
      break;
    default: break;
  }
}

void setup() {
  cbb15::startBoard(console, board, runtime, "BB15 face learning");
  cbb15::loadModel(console, runtime, model(), kModelSlot, kCheckSlotHeader);
  load();
  if (!cam.begin(kCameraResolution, kCameraImageMode, kCameraFrameRate)) {
    cbb15::halt(console, "camera", cbb15::Status::NotStarted);
  }
  if (!stream.begin(frames[0], frames[1], kFrameBytes)) {
    cbb15::halt(console, "camera stream", cbb15::Status::NotStarted);
  }
  // A full OLED update blocks for ~50 ms and would cost camera frames;
  // send it in 64-byte steps of ~6 ms instead, one per loop pass.
  console.setPagedDisplay(true);
  console.logf("m=me o=other");
  console.logf("b=background s=save");
  delay(2000);
  console.clearRows();
}

void loop() {
  while (Serial.available() > 0) {
    command(static_cast<char>(Serial.read()));
  }
  if (!grab()) {
    return;
  }
  cbb15::InferenceResult r;
  const cbb15::Status st = runtime.infer(input, r);
  if (st != cbb15::Status::Ok) {
    console.logf("infer: %s", cbb15::statusName(st));
    return;
  }

  // Decide only among taught classes; absolute potentials are not compared.
  int best = -1;
  float score[kClasses];
  for (int c = 0; c < kClasses; ++c) {
    score[c] = classScore(r, c);
    if ((learnedMask & (1u << c)) && (best < 0 || score[c] > score[best])) {
      best = c;
    }
  }
  memmove(votes + 1, votes, sizeof(int) * (kVoteWindow - 1));
  votes[0] = best;
  voteCount = std::min(voteCount + 1, kVoteWindow);
  int decided = -1;
  for (int c = 0; c < kClasses; ++c) {
    int n = 0;
    for (int i = 0; i < voteCount; ++i) {
      n += votes[i] == c;
    }
    if (n >= kVotesNeeded) {
      decided = c;
    }
  }
  const bool ready = (learnedMask & 0x3) == 0x3 || (learnedMask & 0x5) == 0x5;
  if (decided == kMe && ready) {
    cbb15::setLed(cbb15::LedColor::Green);
  } else if (decided == kOther && ready) {
    cbb15::setLed(cbb15::LedColor::Blue);
  } else {
    cbb15::setLed(cbb15::LedColor::Off);
  }

  console.setRow(1, "%s", ready ? (decided >= 0 ? kClassName[decided] : "?")
                                : "teach m + o or b");
  console.setRow(2, "me %.0f oth %.0f", score[kMe], score[kOther]);
  console.setRow(3, "bg %.0f taught %c%c%c", score[kBackground],
                 (learnedMask & 1) ? 'M' : '-', (learnedMask & 2) ? 'O' : '-',
                 (learnedMask & 4) ? 'B' : '-');
  static cbb15::DutyCycle duty;
  duty.add(r.computeUs);
  // Pure AKD1500 inference time, and the time including the SPI transfer.
  console.setRow(4, "AKD %.1fms tot %.1fms", r.computeUs / 1000.0f,
                 r.latencyUs / 1000.0f);
  cbb15::showDuty(console, duty, 7);
  cbb15::showPower(console, board, 5);
  console.setRow(6, "m o b s l f");
  console.publish();
}
