// BrainBoard1500 example 03 - person detection with the Dev Hub VWW model.
//
// Model: BrainChip Dev Hub akida1/model_zoo/vww (AkidaNet, Visual Wake
//        Words, 96x96x3 RGB, classes "no person" / "person").
// Camera: Nicla Vision GC2145, 320x240 RGB565.
//
// Preprocessing on the Nicla Vision:
//   - The image is rotated by 180 degrees, so the Nicla Vision is held with
//     the micro-USB connector pointing down.
//   - The complete field of view is scaled to 96x96 (no centre crop). The
//     Dev Hub training loads its images with flow_from_directory(
//     target_size=(96, 96)), which also squeezes the whole picture.
//   - 320x240 is the largest RGB565 frame that fits the Nicla Vision RAM
//     without external memory.
//
// Capture: double-buffered (cbb15::CameraStream). The sensor delivers a frame
// every 46.4 ms; Camera::grabFrame() would wait for the next frame start
// after every inference and so use only every second frame. Here the next
// frame is read out while the current one is processed, so the loop runs at
// the sensor's frame rate as long as preprocessing + inference + output stay
// below one frame period.
//
// Output on Serial and OLED: class, probability, latency, frame rate, 0.8 V
// power.
// Before the camera starts, two sample images from the Dev Hub are
// classified and compared with the result of the akida Python simulation.
//
// AKD duty cycle (same definition in every example): share of the elapsed
// time in which the AKD1500 has work in its pipeline,
//   duty = 1 - idle time / elapsed time,  averaged over a window, max. 100 %.
// Sensor capture, preprocessing and the SPI transfers of input and output
// count as idle time.
// Here one job is in flight at a time, so the busy time is the sum of the
// pure inference times measured by the AKD1500 clock counter (2 s windows).

#include <BrainBoard1500.h>
#include "samples_vww.h"
#include "vww.h"
#include <math.h>

#include "camera.h"
#include "gc2145.h"

// Where the model comes from:
//   1..4 = BB15 external flash slot (written by 02_ModelFlasher)
//   0    = do not use the flash, push the model into the AKD1500 via SPI
constexpr uint8_t kModelSlot = 1;
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

constexpr uint16_t kInputSize = 96;

cbb15::Console console;
cbb15::Board board;
cbb15::Akida runtime;
GC2145 galaxyCore;
Camera cam(galaxyCore);
cbb15::CameraStream stream;
// Two frames for the double buffer, in AXI SRAM where the DMA can reach them.
alignas(32) uint8_t frames[2][kFrameBytes];
uint8_t input[kInputSize * kInputSize * 3];

const cbb15::Model& model() {
  static const cbb15::Model m = kModelSlot == 0
                                    ? cbb15::models::vwwWithProgram()
                                    : cbb15::models::vww();
  return m;
}

// Probability of "person" from the two dequantised class scores (softmax).
float personProbability(const cbb15::InferenceResult& r) {
  return 1.0f / (1.0f + expf(r.scores[0] - r.scores[1]));
}

void sampleTest() {
  using namespace cbb15::samples;
  for (uint16_t i = 0; i < samples_vww_count; ++i) {
    cbb15::InferenceResult r;
    const cbb15::Status st = runtime.infer(samples_vww_data[i], r);
    if (st != cbb15::Status::Ok) {
      cbb15::halt(console, "sample", st);
    }
    console.logf("sample '%s': %s", samples_vww_truth[i],
                 r.best == samples_vww_expected[i] ? "PASS" : "FAIL");
  }
}

void setup() {
  cbb15::startBoard(console, board, runtime, "BB15 person detect");
  cbb15::loadModel(console, runtime, model(), kModelSlot, kCheckSlotHeader);
  sampleTest();
  if (!cam.begin(kCameraResolution, kCameraImageMode, kCameraFrameRate)) {
    cbb15::halt(console, "camera", cbb15::Status::NotStarted);
  }
  console.logf("camera 320x240 ok");
  delay(1000);
  if (!stream.begin(frames[0], frames[1], kFrameBytes)) {
    cbb15::halt(console, "camera stream", cbb15::Status::NotStarted);
  }
  // A full OLED update blocks for ~50 ms and would cost a camera frame;
  // send it in 64-byte steps of ~6 ms instead, one per loop pass.
  console.setPagedDisplay(true);
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
  const uint32_t t1 = micros();
  // Frame rate from the second frame on (the first one is waiting already).
  if (lastFrameUs != 0) {
    const float frameFps = 1e6f / static_cast<float>(t1 - lastFrameUs);
    fps = fps == 0 ? frameFps : 0.9f * fps + 0.1f * frameFps;
  }
  lastFrameUs = t1;

  cbb15::FrameFormat format;  // 320x240, big-endian RGB565, rotate 180
  cbb15::rgb565ToRgb(pixels, format, cbb15::fullFrame(format), input,
                     kInputSize, kInputSize);
  const uint32_t t2 = micros();

  cbb15::InferenceResult r;
  const cbb15::Status st = runtime.infer(input, r);
  if (st != cbb15::Status::Ok) {
    console.logf("infer: %s", cbb15::statusName(st));
    return;
  }
  duty.add(r.computeUs);
  const float p = personProbability(r);
  const bool person = r.best == 1;
  cbb15::setLed(person ? cbb15::LedColor::Green : cbb15::LedColor::Off);

  console.setRow(1, "%s", person ? "PERSON" : "no person");
  console.setRow(2, "p(person) %.2f", p);
  // Pure AKD1500 inference time, and the time including the SPI transfer.
  console.setRow(3, "AKD %.1fms tot %.1fms", r.computeUs / 1000.0f,
                 r.latencyUs / 1000.0f);
  // Frame rate, and how long the loop waited for the frame (spare time).
  console.setRow(4, "%.1f fps wait %lu ms", fps,
                 static_cast<unsigned long>(stream.waitUs() / 1000));
  cbb15::showDuty(console, duty, 5);
  cbb15::showPower(console, board, 6);
  console.setRow(7, "luma %u prep %lu ms",
                 cbb15::meanLuma(input, kInputSize * kInputSize),
                 static_cast<unsigned long>((t2 - t1) / 1000));
  console.publish();
}
