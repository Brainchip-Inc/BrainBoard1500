// BrainBoard1500 example 09 - keep the AKD1500 busy as close to 100 % as possible.
//
// In the camera examples the AKD1500 computes for ~5 ms per frame and then
// waits: for the camera (~60 ms), the preprocessing (~8 ms) and the SPI
// transfer of the 27,648-byte input (~10 ms at 25 MHz). This example removes
// all of that from the AKD1500's critical path:
//
//   - The model (VWW person detection) boots from flash slot 1: no program
//     transfer over SPI.
//   - The input image is written into the AKD1500 SRAM once. Every job then
//     reads it from there (enqueue by address): no image data crosses the
//     SPI bus per inference.
//   - The AKD1500 hardware pipeline holds up to 15 jobs. The loop only
//     fetches finished jobs (a few bytes each) and immediately refills the
//     queue, so a new job is always waiting when one finishes.
//
// AKD duty cycle (same definition in every example): share of the elapsed
// time in which the AKD1500 has work in its pipeline,
//   duty = 1 - idle time / elapsed time,  averaged over a window, max. 100 %.
// Sensor capture, preprocessing and the SPI transfers of input and output
// count as idle time.
// Here up to 15 jobs are queued. The pipeline counts as idle only when a
// loop pass finds every job finished; then the whole time since the previous
// pass is booked as idle, so the value errs on the low side (1 s windows).
//
// Why "inferences/s x time of one job" does not work: one job alone takes
// ~5.6 ms from input to output (AKD1500 clock counter), but in the pipeline
// the layers of the model run on different NPs, so several jobs are processed
// at the same time - one in the first layers, the next ones further down the
// mesh. The example therefore also shows the time between two results and
// the resulting number of overlapping jobs (inferences/s x single-job time).
//
// Every kRefreshMs the input is replaced by a new camera frame (one SPI
// transfer) so the result still follows the scene; set kUseCamera = false
// for a fixed sample image and no transfers at all.
//
// Output on Serial and OLED: inferences/s, duty cycle, pure inference time,
// class of the newest job, and the 0.8 V power at full load.
// LED: green = the newest job sees a person, off = no person.

#include <BrainBoard1500.h>
#include "samples_vww.h"
#include "vww.h"

#include "camera.h"
#include "gc2145.h"

// Where the model comes from:
//   1..4 = BB15 external flash slot (written by 02_ModelFlasher)
//   0    = do not use the flash, push the model into the AKD1500 via SPI
constexpr uint8_t kModelSlot = 1;
constexpr bool kCheckSlotHeader = true;

// Jobs kept in the AKD1500 pipeline (the engine allows up to 15).
constexpr uint8_t kPipelineDepth = 15;
// Replace the input with a camera frame this often (0 = never).
constexpr bool kUseCamera = true;
constexpr uint32_t kRefreshMs = 2000;
// Report interval.
constexpr uint32_t kReportMs = 1000;

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
// Double-buffered capture (cbb15::CameraStream): the next frame is read
// out while the current one is processed. Both frames in AXI SRAM, where
// the DMA can reach them.
cbb15::CameraStream stream;
alignas(32) uint8_t frames[2][kFrameBytes];
uint8_t input[kInputSize * kInputSize * 3];
uint32_t singleJobUs = 0;  // pure inference time of one job

const cbb15::Model& model() {
  static const cbb15::Model m = kModelSlot == 0
                                    ? cbb15::models::vwwWithProgram()
                                    : cbb15::models::vww();
  return m;
}

// Converts the next camera frame into the model input. With timeoutMs = 0
// it only takes a frame that is already complete and never blocks.
bool grabInput(uint32_t timeoutMs) {
  if (timeoutMs == 0 && !stream.ready()) {
    return false;
  }
  const uint8_t* pixels = stream.next(timeoutMs == 0 ? 1 : timeoutMs);
  if (pixels == nullptr) {
    return false;
  }
  cbb15::FrameFormat format;  // 320x240, big-endian RGB565, rotate 180
  cbb15::rgb565ToRgb(pixels, format, cbb15::fullFrame(format), input,
                     kInputSize, kInputSize);
  return true;
}

void setup() {
  cbb15::startBoard(console, board, runtime, "BB15 max duty cycle");
  cbb15::loadModel(console, runtime, model(), kModelSlot, kCheckSlotHeader);

  memcpy(input, cbb15::samples::samples_vww_data[0], sizeof(input));
  if (kUseCamera) {
    if (!cam.begin(kCameraResolution, kCameraImageMode, kCameraFrameRate)) {
      cbb15::halt(console, "camera", cbb15::Status::NotStarted);
    }
    if (!stream.begin(frames[0], frames[1], kFrameBytes)) {
      cbb15::halt(console, "camera stream", cbb15::Status::NotStarted);
    }
    grabInput(3000);
  }

  // One ordinary inference: the AKD1500 clock counter gives the pure
  // inference time of a single job, the reference for the duty cycle.
  cbb15::InferenceResult r;
  uint64_t sum = 0;
  for (int i = 0; i < 8; ++i) {
    const cbb15::Status st = runtime.infer(input, r);
    if (st != cbb15::Status::Ok) {
      cbb15::halt(console, "single job", st);
    }
    sum += r.computeUs;
  }
  singleJobUs = static_cast<uint32_t>(sum / 8);
  console.logf("1 job: AKD %.2f ms", singleJobUs / 1000.0f);
  console.logf("with SPI: %.2f ms", r.latencyUs / 1000.0f);

  const cbb15::Status st = runtime.startStream(input, kPipelineDepth);
  if (st != cbb15::Status::Ok) {
    cbb15::halt(console, "stream", st);
  }
  console.logf("pipeline depth %u", runtime.streamDepth());
  delay(1500);
  console.clearRows();
}

void loop() {
  static uint32_t windowStart = millis();
  static uint32_t lastRefresh = millis();
  static bool frameWanted = false;
  static uint32_t jobs = 0;
  static cbb15::InferenceResult latest;

  uint32_t done = 0;
  const cbb15::Status st = runtime.streamStep(done, &latest);
  if (st != cbb15::Status::Ok) {
    console.logf("stream: %s", cbb15::statusName(st));
    delay(100);
    return;
  }
  jobs += done;

  // LED follows the newest result. Only switched on a change: setLed() costs
  // several pin writes, and this loop runs hundreds of times per second.
  static int8_t ledPerson = -1;
  if (done > 0) {
    const int8_t person = latest.best == 1 ? 1 : 0;
    if (person != ledPerson) {
      ledPerson = person;
      cbb15::setLed(person ? cbb15::LedColor::Green : cbb15::LedColor::Off);
    }
  }

  // Occasional new camera frame: one 27,648-byte transfer every kRefreshMs.
  // The frame waiting in the camera buffer is up to kRefreshMs old, so it is
  // dropped and a new capture started; the loop keeps serving the AKD1500
  // pipeline and takes the frame once it is complete (about 50 ms later).
  if (kUseCamera && millis() - lastRefresh >= kRefreshMs) {
    lastRefresh = millis();
    stream.skipFinished();
    frameWanted = true;
  }
  if (frameWanted && grabInput(0)) {
    frameWanted = false;
    runtime.updateStreamInput(input);
  }

  const uint32_t elapsed = millis() - windowStart;
  if (elapsed >= kReportMs) {
    const float rate = jobs * 1000.0f / elapsed;
    // Busy share: time with queued work in the AKD1500 pipeline.
    const float idleMs = runtime.takeStreamIdleUs() / 1000.0f;
    float duty = 100.0f * (1.0f - idleMs / elapsed);
    duty = duty < 0.0f ? 0.0f : (duty > 100.0f ? 100.0f : duty);
    // Time between two results and the number of jobs in flight at once.
    const float intervalMs = rate > 0.0f ? 1000.0f / rate : 0.0f;
    const float overlap = rate * singleJobUs / 1e6f;
    console.setRow(1, "%.1f inferences/s", rate);
    console.setRow(2, "AKD duty %.1f%%", duty);
    console.setRow(3, "1 job %.2fms / %.2fms", singleJobUs / 1000.0f, intervalMs);
    console.setRow(4, "%.1f jobs overlap", overlap);
    console.setRow(5, "%s depth %u",
                   latest.best == 1 ? "PERSON" : latest.best == 0 ? "no person" : "-",
                   runtime.streamDepth());
    cbb15::showPower(console, board, 6);
    console.publish(true);
    windowStart = millis();
    jobs = 0;
  }
}
