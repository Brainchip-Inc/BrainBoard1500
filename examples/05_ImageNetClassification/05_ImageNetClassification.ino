// BrainBoard1500 example 05 - 1000-class image classification (ImageNet).
//
// Model: BrainChip Dev Hub akida1/model_zoo/imagenet_akidanet,
//        AkidaNet alpha 0.25, 160x160x3 RGB, 1000 ImageNet classes.
// Camera: Nicla Vision GC2145, 320x240 RGB565, rotated by 180 degrees
//         (micro-USB connector pointing down).
//
// Preprocessing exactly like the Dev Hub (imagenet_akidanet_preprocessing.py):
// the shorter image side is scaled to 183 pixels and the central 160x160 is
// used. On a 320x240 frame that is the central 210x210 square, which is
// resampled to 160x160. Pixels stay 0..255; the model rescales internally.
//
// The 1000-class classifier is an FNP2 layer whose weights the AKD1500 reads
// for every inference, so the library copies the program data from slot 3
// into the AKD1500 SRAM at load and the weights are read from there.
// Two images of the BrainChip ImageNet-like sample pack are classified at
// start-up and compared with the akida Python simulation.
//
// AKD duty cycle (same definition in every example): share of the elapsed
// time in which the AKD1500 has work in its pipeline,
//   duty = 1 - idle time / elapsed time,  averaged over a window, max. 100 %.
// Sensor capture, preprocessing and the SPI transfers of input and output
// count as idle time.
// Here one job is in flight at a time, so the busy time is the sum of the
// pure inference times measured by the AKD1500 clock counter (2 s windows).

#include <BrainBoard1500.h>
#include "imagenet.h"
#include "samples_imagenet.h"
#include <math.h>

#include "camera.h"
#include "gc2145.h"

// Where the model comes from:
//   1..4 = BB15 external flash slot (written by 02_ModelFlasher)
//   0    = do not use the flash, push the model into the AKD1500 via SPI
constexpr uint8_t kModelSlot = 3;
// Read the slot header once at start-up to make sure the slot holds this
// model (about 0.5 s through the flash bridge). The flash probe itself is
// always skipped (BoardConfig::assumeFlashReady).
constexpr bool kCheckSlotHeader = true;

constexpr uint8_t kCameraResolution = CAMERA_R320x240;
constexpr uint8_t kCameraImageMode = CAMERA_RGB565;
// Has no effect: GC2145::setFrameRate() is empty in the Arduino core, the
// sensor runs at a fixed 21.6 frames/s at 320x240 (measured).
constexpr int kCameraFrameRate = 15;
constexpr size_t kFrameBytes = 320 * 240 * 2;

constexpr uint16_t kInputSize = 160;
// 160 / 183 of the shorter side (240 px) = 210 px centre square.
constexpr float kCropFraction = 160.0f / 183.0f;

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

const cbb15::Model& model() {
  static const cbb15::Model m = kModelSlot == 0
                                    ? cbb15::models::imagenetWithProgram()
                                    : cbb15::models::imagenet();
  return m;
}

// Softmax probability of the best class.
float topProbability(const cbb15::InferenceResult& r) {
  double sum = 0;
  for (float v : r.scores) {
    sum += exp(static_cast<double>(v - r.bestScore));
  }
  return static_cast<float>(1.0 / sum);
}

void sampleTest() {
  using namespace cbb15::samples;
  for (uint16_t i = 0; i < samples_imagenet_count; ++i) {
    cbb15::InferenceResult r;
    const cbb15::Status st = runtime.infer(samples_imagenet_data[i], r);
    if (st != cbb15::Status::Ok) {
      cbb15::halt(console, "sample", st);
    }
    console.logf("sample %u: %s", i,
                 r.best == samples_imagenet_expected[i] ? "PASS" : "FAIL");
    console.logf(" %.20s", model().label(r.best));
  }
}

void setup() {
  cbb15::startBoard(console, board, runtime, "BB15 ImageNet");
  cbb15::loadModel(console, runtime, model(), kModelSlot, kCheckSlotHeader);
  sampleTest();
  if (!cam.begin(kCameraResolution, kCameraImageMode, kCameraFrameRate)) {
    cbb15::halt(console, "camera", cbb15::Status::NotStarted);
  }
  if (!stream.begin(frames[0], frames[1], kFrameBytes)) {
    cbb15::halt(console, "camera stream", cbb15::Status::NotStarted);
  }
  // A full OLED update blocks for ~50 ms and would cost camera frames;
  // send it in 64-byte steps of ~6 ms instead, one per loop pass.
  console.setPagedDisplay(true);
  delay(1500);
  console.clearRows();
}

void loop() {
  static cbb15::DutyCycle duty;
  static uint32_t lastFrameUs = 0;  // 0 = no frame yet
  static float fps = 0;
  const uint8_t* pixels = stream.next(3000);
  if (pixels == nullptr) {
    console.logf("frame timeout");
    return;
  }
  const uint32_t frameUs = micros();
  // Frame rate from the second frame on (the first one is waiting already).
  if (lastFrameUs != 0) {
    const float frameFps = 1e6f / static_cast<float>(frameUs - lastFrameUs);
    fps = fps == 0 ? frameFps : 0.9f * fps + 0.1f * frameFps;
  }
  lastFrameUs = frameUs;
  cbb15::FrameFormat format;
  cbb15::rgb565ToRgb(pixels, format,
                     cbb15::centreSquare(format, kCropFraction), input,
                     kInputSize, kInputSize);
  cbb15::InferenceResult r;
  const cbb15::Status st = runtime.infer(input, r);
  if (st != cbb15::Status::Ok) {
    console.logf("infer: %s", cbb15::statusName(st));
    return;
  }
  // Second best class for the display.
  int second = -1;
  for (size_t i = 0; i < r.scores.size(); ++i) {
    if (static_cast<int>(i) != r.best &&
        (second < 0 || r.scores[i] > r.scores[second])) {
      second = static_cast<int>(i);
    }
  }
  console.setRow(1, "%.21s", model().label(r.best));
  console.setRow(2, "p %.2f  #%d", topProbability(r), static_cast<int>(r.best));
  console.setRow(3, "2nd %.17s", model().label(second));
  duty.add(r.computeUs);
  // Pure AKD1500 inference time, and the time including the SPI transfer.
  console.setRow(4, "AKD %.1fms tot %.1fms", r.computeUs / 1000.0f,
                 r.latencyUs / 1000.0f);
  cbb15::showPower(console, board, 5);
  cbb15::showDuty(console, duty, 6);
  // Frame rate, and how long the loop waited for the frame (spare time).
  console.setRow(7, "%.1f fps wait %lu ms", fps,
                 static_cast<unsigned long>(stream.waitUs() / 1000));
  console.publish();
}
