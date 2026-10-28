#ifndef NICLA_VISION_CONNECT_VISION_PIPELINE_H_
#define NICLA_VISION_CONNECT_VISION_PIPELINE_H_

#include <Arduino.h>
#include <BrainBoard1500.h>

namespace vision {

/** @brief Input the human detection model takes, and the preview's size. */
constexpr uint16_t kFrameWidth = 96u;
constexpr uint16_t kFrameHeight = 96u;

/** @brief Bytes in one preview frame, which is the model input in grayscale. */
constexpr size_t kPreviewBytes =
    static_cast<size_t>(kFrameWidth) * kFrameHeight;

/** @brief Reported as the predicted index when nothing has been scored yet. */
constexpr uint8_t kNoPrediction = 0xFFu;

/** @brief What the pipeline needs to know about the model now loaded. */
struct ModelParameters {
  uint8_t classCount = 0u;
};

/**
 * @brief One scored frame.
 *
 * `person` is what this frame on its own looked like, which is what the LED
 * follows. `triggered` is the detector's decision that a person is there and
 * this is the frame to tell the phone about.
 */
struct Detection {
  uint8_t classIndex = kNoPrediction;
  float confidence = 0.0f;
  bool person = false;
  bool triggered = false;
};

/** @brief Called once per scored frame, detection or not. */
using DetectionHandler = void (*)(const Detection& detection);

/**
 * @brief Called with the preview of the frame that was just scored.
 *
 * @param pixels  kPreviewBytes grayscale pixels, top row first.
 */
using PreviewHandler = void (*)(const uint8_t* pixels);

/**
 * @brief Start the camera.
 *
 * The capture buffers are not allocated here. They are taken when scoring
 * starts and given back when it stops, because together they are more than
 * half of the RAM and keyword spotting has no use for them.
 *
 * @return True when the sensor answered and is configured. Scoring never
 *         starts without it.
 */
bool begin();

/**
 * @brief Adopt a model that the caller has already loaded on the BrainBoard.
 *
 * @param runtime     Runtime holding the loaded model, or nullptr to stop.
 * @param parameters  The loaded model's own metadata. Ignored when runtime is
 *                    nullptr.
 * @return True when the metadata describes a model this pipeline can score.
 */
bool setModel(cbb15::Akida* runtime, const ModelParameters& parameters);

/** @brief Say whether a model has been adopted and can be scored. */
bool modelReady();

/**
 * @brief Start or stop turning camera frames into inferences.
 *
 * Stopping releases the camera's two capture buffers, which are 300 kB and
 * the largest single thing this demo holds.
 *
 * @param running  True to score frames.
 */
void setInferenceRunning(bool running);

/**
 * @brief Start or stop sending the preview to the phone.
 *
 * @param streaming  True to call the preview handler once per scored frame.
 */
void setPreviewStreaming(bool streaming);

/**
 * @brief Register the callbacks the pipeline reports through.
 *
 * @param onDetection  Called once per scored frame.
 * @param onPreview    Called after onDetection while streaming is on.
 */
void setHandlers(DetectionHandler onDetection, PreviewHandler onPreview);

/**
 * @brief Score the camera's newest frame, if one has finished.
 *
 * It never waits for the camera: the next frame is captured into the other
 * buffer while this one is scored, so a call with no finished frame returns at
 * once and the sketch's loop stays free to answer the phone. Call it often.
 *
 * @return True when a frame was scored on this call.
 */
bool poll();

/**
 * @brief Label for one class of the loaded model.
 *
 * @param classIndex  Index into the model's output.
 * @return The label, or a numbered placeholder past the known classes.
 */
const char* classLabel(uint8_t classIndex);

}  // namespace vision

#endif  // NICLA_VISION_CONNECT_VISION_PIPELINE_H_
