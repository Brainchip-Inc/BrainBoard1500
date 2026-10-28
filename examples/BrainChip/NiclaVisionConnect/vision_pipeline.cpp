#include "vision_pipeline.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "camera.h"
#include "gc2145.h"

namespace vision {
namespace {

constexpr int32_t kCameraResolution = CAMERA_R320x240;
constexpr int32_t kCameraImageMode = CAMERA_RGB565;
constexpr int32_t kCameraFrameRate = 30;
constexpr size_t kCaptureBytes = 320u * 240u * 2u;
constexpr uint32_t kCaptureTimeoutMs = 3000u;

constexpr uint16_t kModelChannels = 3u;
constexpr size_t kModelInputBytes =
    static_cast<size_t>(kFrameWidth) * kFrameHeight * kModelChannels;

// The model scores two classes, and this is the one that means a person.
constexpr uint8_t kPersonClass = 1u;
constexpr const char* kClassLabels[] = {"no_person", "person"};
constexpr uint8_t kClassLabelCount =
    sizeof(kClassLabels) / sizeof(kClassLabels[0]);
constexpr uint8_t kMaxClasses = 8u;

// The person score has to reach this and hold it for this many frames in a row
// before a detection is reported. Both come from the keyword detector, which
// decides the same way.
constexpr float kScoreThreshold = 0.50f;
constexpr uint8_t kChimingThreshold = 3u;

ModelParameters g_model;
cbb15::Akida* g_runtime = nullptr;

DetectionHandler g_on_detection = nullptr;
PreviewHandler g_on_preview = nullptr;

uint8_t g_chiming = 0u;
uint8_t g_lapsed = 0u;
bool g_armed = true;

cbb15::CameraStream g_stream;
// Static, as in 03_PersonDetection, because once models have been sent and
// swapped the heap cannot be relied on for 300 kB in one piece. The camera DMA
// writes straight into them, and the stream refuses a buffer off a cache line.
alignas(32) uint8_t g_capture[2][kCaptureBytes];
bool g_capturing = false;

uint8_t g_model_input[kModelInputBytes];
uint8_t g_preview[kPreviewBytes];

bool g_running = false;
bool g_streaming = false;
bool g_camera_ready = false;
bool g_failure_reported = false;

/** @brief The camera driver's sensor, made on first use. */
GC2145& sensor() {
  static GC2145 instance;
  return instance;
}

/** @brief The camera, made on first use. */
Camera& camera() {
  static Camera instance(sensor());
  return instance;
}

/**
 * @brief Start the camera filling the two capture buffers, or stop it.
 *
 * @param wanted  True to capture, false to stop.
 * @return True when capture is in the state asked for.
 */
bool set_capturing(bool wanted) {
  if (wanted == g_capturing) {
    return true;
  }
  if (wanted) {
    g_capturing = g_stream.begin(g_capture[0], g_capture[1], kCaptureBytes);
  } else {
    g_stream.end();
    g_capturing = false;
  }
  return wanted == g_capturing;
}

/**
 * @brief Turn one RGB565 capture into the model's input and its preview.
 *
 * The whole field of view is squeezed to the model's square input and turned
 * half a turn, which is what 03_PersonDetection does: the Dev Hub trained the
 * model on whole images resized to 96x96, and the camera is mounted USB-side
 * down. The preview is the luminance of the same pixels, so what the phone
 * shows is what the network was given.
 *
 * @param frame  Whole RGB565 capture from the sensor.
 */
void build_model_input(const uint8_t* frame) {
  const cbb15::FrameFormat format;
  cbb15::rgb565ToRgb(frame, format, cbb15::fullFrame(format), g_model_input,
                     kFrameWidth, kFrameHeight);
  for (size_t pixel = 0u; pixel < kPreviewBytes; ++pixel) {
    const uint8_t* rgb = &g_model_input[pixel * kModelChannels];
    g_preview[pixel] = static_cast<uint8_t>(
        (77u * rgb[0] + 150u * rgb[1] + 29u * rgb[2]) >> 8);
  }
}

/**
 * @brief Numerically stable softmax in place.
 *
 * @param values  One value per class, replaced by the normalized result.
 */
void softmax_in_place(float* values) {
  float highest = values[0];
  for (uint8_t index = 1u; index < g_model.classCount; ++index) {
    if (values[index] > highest) {
      highest = values[index];
    }
  }
  float total = 0.0f;
  for (uint8_t index = 0u; index < g_model.classCount; ++index) {
    values[index] = expf(values[index] - highest);
    total += values[index];
  }
  for (uint8_t index = 0u; index < g_model.classCount; ++index) {
    values[index] /= total;
  }
}

/** @brief Clear the decision counters and let the detector fire again. */
void reset_decision_state() {
  g_chiming = 0u;
  g_lapsed = 0u;
  g_armed = true;
}

/**
 * @brief Say whether this frame is the one to report a person on.
 *
 * The score has to hold above the threshold for kChimingThreshold frames,
 * which is how the keyword detector decides. What arms that one again is the
 * word ending; a person does not end, they stay in view, so this one arms
 * again only once the score has fallen back for as many frames as it took to
 * fire. A person is therefore reported when they arrive and not again until
 * they have gone.
 *
 * @param personScore  This frame's score for the person class, or zero when
 *                     the frame went the other way.
 * @return True when this frame should be reported to the phone.
 */
bool apply_decision(float personScore) {
  if (personScore >= kScoreThreshold) {
    g_lapsed = 0u;
    if (g_chiming < kChimingThreshold) {
      ++g_chiming;
    }
    if (g_armed && g_chiming >= kChimingThreshold) {
      g_armed = false;
      return true;
    }
    return false;
  }

  g_chiming = 0u;
  if (g_lapsed < kChimingThreshold) {
    ++g_lapsed;
  }
  if (g_lapsed >= kChimingThreshold) {
    g_armed = true;
  }
  return false;
}

/**
 * @brief Score one frame and report it.
 *
 * @param frame  Whole RGB565 capture from the sensor.
 * @return True when the frame was scored.
 */
bool score_frame(const uint8_t* frame) {
  build_model_input(frame);

  cbb15::InferenceResult result;
  const cbb15::Status status = g_runtime->infer(g_model_input, result);
  if (status != cbb15::Status::Ok ||
      result.scores.size() < g_model.classCount) {
    if (!g_failure_reported) {
      g_failure_reported = true;
      Serial.print("[vision] inference failed: ");
      Serial.println(cbb15::statusName(status));
    }
    return false;
  }

  // The runtime hands back dequantized potentials, which is what is softmaxed.
  float scores[kMaxClasses];
  memcpy(scores, result.scores.data(), g_model.classCount * sizeof(float));
  softmax_in_place(scores);

  Detection detection;
  detection.classIndex = 0u;
  detection.confidence = scores[0];
  for (uint8_t index = 1u; index < g_model.classCount; ++index) {
    if (scores[index] > detection.confidence) {
      detection.classIndex = index;
      detection.confidence = scores[index];
    }
  }
  detection.person = detection.classIndex == kPersonClass;
  detection.triggered =
      apply_decision(detection.person ? detection.confidence : 0.0f);

  // The result goes first and on its own, so the phone's indicator keeps up
  // with the board even while a preview frame is still going out.
  if (g_on_detection != nullptr) {
    g_on_detection(detection);
  }
  if (g_streaming && g_on_preview != nullptr) {
    g_on_preview(g_preview);
  }
  return true;
}

}  // namespace

bool begin() {
  g_camera_ready =
      camera().begin(kCameraResolution, kCameraImageMode, kCameraFrameRate);
  return g_camera_ready;
}

bool setModel(cbb15::Akida* runtime, const ModelParameters& parameters) {
  g_runtime = nullptr;
  g_model = ModelParameters();

  if (runtime == nullptr || parameters.classCount <= kPersonClass ||
      parameters.classCount > kMaxClasses) {
    return false;
  }
  g_runtime = runtime;
  g_model = parameters;
  return true;
}

bool modelReady() { return g_runtime != nullptr; }

void setInferenceRunning(bool running) {
  if (running == g_running) {
    return;
  }
  if (running && !g_camera_ready) {
    Serial.println("[vision] not started: the camera did not answer at boot");
  }
  g_running = running && modelReady() && g_camera_ready && set_capturing(true);
  g_failure_reported = false;
  reset_decision_state();
  if (!g_running) {
    set_capturing(false);
  }
}

void setPreviewStreaming(bool streaming) { g_streaming = streaming; }

void setHandlers(DetectionHandler onDetection, PreviewHandler onPreview) {
  g_on_detection = onDetection;
  g_on_preview = onPreview;
}

bool poll() {
  if (!g_running || !g_stream.ready()) {
    return false;
  }
  // A finished frame is waiting, so this takes it without waiting and starts
  // the capture of the next one into the other buffer.
  const uint8_t* frame = g_stream.next(kCaptureTimeoutMs);
  return frame != nullptr && score_frame(frame);
}

const char* classLabel(uint8_t classIndex) {
  if (classIndex < kClassLabelCount) {
    return kClassLabels[classIndex];
  }
  static char numbered[12];
  snprintf(numbered, sizeof(numbered), "class %u",
           static_cast<unsigned>(classIndex));
  return numbered;
}

}  // namespace vision
