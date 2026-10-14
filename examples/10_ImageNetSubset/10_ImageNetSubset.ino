// BrainBoard1500 example 10 - image classification with your own 100 ImageNet classes.
//
// Model: imagenet_subset, made by tools/make_imagenet_subset.py from the Dev
//        Hub AkidaNet (alpha 0.25, 160x160x3). The 1000-class classifier is
//        cut down to the classes listed in tools/imagenet_subset.txt (by
//        default 100 everyday objects: kitchen, desk, home, clothes, pets,
//        toys, tools). No training is involved: the potentials of the chosen
//        classes are bit-identical to the 1000-class model, but classes you do
//        not care about can no longer win.
//
// To choose your own classes: edit tools/imagenet_subset.txt (ImageNet index
// or name per line, up to 100), run
//   python tools/make_imagenet_subset.py --devhub <brainchip_devhub>
//          --imagenet-labels imagenet_labels2names.py
// and upload this sketch again.
//
// Camera and preprocessing as in 05_ImageNetClassification: GC2145 320x240
// RGB565, rotated by 180 degrees (micro-USB connector down), central 210x210
// square (Dev Hub geometry: shorter side 183 px, centre 160 px) -> 160x160.
// The scores are averaged over the last kSmoothFrames frames, and a class is
// only reported above kMinProbability (35 %); below that nothing is shown.
//
// AKD duty cycle (same definition in every example): share of the elapsed
// time in which the AKD1500 has work in its pipeline,
//   duty = 1 - idle time / elapsed time,  averaged over a window, max. 100 %.
// Sensor capture, preprocessing and the SPI transfers of input and output
// count as idle time.
// Here one job is in flight at a time, so the busy time is the sum of the
// pure inference times measured by the AKD1500 clock counter (2 s windows).

#include <BrainBoard1500.h>
#include "imagenet_subset.h"
#include <math.h>

#include <vector>

#include "camera.h"
#include "gc2145.h"

// Where the model comes from:
//   1..4 = BB15 external flash slot (written by 02_ModelFlasher)
//   0    = do not use the flash, push the model into the AKD1500 via SPI
// The 248 KB program is pushed over SPI by default (about 0.1 s), so the
// flash slots stay as they are. To boot it from flash, add
// {3, cbb15::models::imagenetSubsetWithProgram()} to 02_ModelFlasher.
constexpr uint8_t kModelSlot = 0;
constexpr bool kCheckSlotHeader = true;

constexpr uint8_t kCameraResolution = CAMERA_R320x240;
constexpr uint8_t kCameraImageMode = CAMERA_RGB565;
// Has no effect: GC2145::setFrameRate() is empty in the Arduino core, the
// sensor runs at a fixed 21.6 frames/s at 320x240 (measured).
constexpr int kCameraFrameRate = 15;
constexpr size_t kFrameBytes = 320 * 240 * 2;

constexpr uint16_t kInputSize = 160;
constexpr float kCropFraction = 160.0f / 183.0f;
constexpr int kSmoothFrames = 3;
constexpr float kMinProbability = 0.35f;

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
std::vector<float> smoothed;

const cbb15::Model& model() {
  static const cbb15::Model m = kModelSlot == 0
                                    ? cbb15::models::imagenetSubsetWithProgram()
                                    : cbb15::models::imagenetSubset();
  return m;
}

// Softmax over the selected classes.
void softmax(const std::vector<float>& in, std::vector<float>& out) {
  float mx = in[0];
  for (float v : in) {
    mx = v > mx ? v : mx;
  }
  double sum = 0;
  out.resize(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    out[i] = expf(in[i] - mx);
    sum += out[i];
  }
  for (float& v : out) {
    v = static_cast<float>(v / sum);
  }
}

void setup() {
  cbb15::startBoard(console, board, runtime, "BB15 my 100 classes");
  cbb15::loadModel(console, runtime, model(), kModelSlot, kCheckSlotHeader);
  console.logf("%u classes", model().labelCount);
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
  duty.add(r.computeUs);

  // Probabilities, averaged over the last frames (exponential smoothing).
  std::vector<float> p;
  softmax(r.scores, p);
  if (smoothed.size() != p.size()) {
    smoothed = p;
  } else {
    const float a = 1.0f / kSmoothFrames;
    for (size_t i = 0; i < p.size(); ++i) {
      smoothed[i] = (1.0f - a) * smoothed[i] + a * p[i];
    }
  }
  // Best class. Only it is shown, and only above kMinProbability; nothing
  // otherwise.
  size_t best = 0;
  for (size_t i = 1; i < smoothed.size(); ++i) {
    if (smoothed[i] > smoothed[best]) {
      best = i;
    }
  }

  const bool sure = smoothed[best] > kMinProbability;
  cbb15::setLed(sure ? cbb15::LedColor::Green : cbb15::LedColor::Off);
  if (sure) {
    console.setRow(1, "%.21s", model().label(best));
    console.setRow(2, "p %.2f", smoothed[best]);
  } else {
    console.setRow(1, "-");
    console.setRow(2, "%s", "");
  }
  // Frame rate, and how long the loop waited for the frame (spare time).
  console.setRow(4, "%.1f fps wait %lu ms", fps,
                 static_cast<unsigned long>(stream.waitUs() / 1000));
  console.setRow(5, "AKD %.1fms tot %.1fms", r.computeUs / 1000.0f,
                 r.latencyUs / 1000.0f);
  cbb15::showPower(console, board, 6);
  cbb15::showDuty(console, duty, 7);
  console.publish();
}
