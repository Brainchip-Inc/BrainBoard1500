// BrainBoard1500 - speaker code for the voice learning example.
//
// Describes an utterance with two speaker cues and encodes them as the
// 304-bit binary input of the AKD1500 learning layer (model voice_learn):
//   1. spectral envelope: mean level (dB) of 32 mel bands (60..7600 Hz) over
//      the voiced frames, minus its own mean, quantised to 8 levels per band
//      around a per-band centre (edges every 4 dB, two bits set per band)
//   2. pitch: median F0 (autocorrelation, 70..400 Hz) in 24 log-spaced bins,
//      three adjacent bits set, repeated twice
//
// A keyword network is trained to ignore the speaker, which is why this
// example does not reuse the keyword features. tools/cbb15_speaker.py is
// the NumPy reference of the same computation.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cbb15 {

class SpeakerEncoder {
 public:
  static constexpr int kSampleRate = 16000;
  static constexpr int kFrameLength = 480;
  static constexpr int kFrameStep = 320;
  static constexpr int kFftLength = 512;
  static constexpr int kBands = 32;
  static constexpr int kLevels = 8;
  static constexpr float kStepDb = 4.0f;
  static constexpr float kVoicedRangeDb = 25.0f;
  static constexpr int kPitchBins = 24;
  static constexpr float kPitchMinHz = 70.0f;
  static constexpr float kPitchMaxHz = 400.0f;
  static constexpr int kBits = kBands * kLevels + 2 * kPitchBins;  // 304
  static constexpr int kMaxFrames = 1 + (32000 - kFrameLength) / kFrameStep;

  SpeakerEncoder();
  ~SpeakerEncoder();
  // bandCentre: kBands values exported with the model (dB).
  bool begin(const float* bandCentre);

  // Analyses up to two seconds of 16 kHz audio and writes kBits values
  // (0 or 1) to bits. Returns false if no voiced frame was found.
  bool encode(const int16_t* audio, size_t count, uint8_t* bits);

  // Results of the last encode() call, for display and debugging.
  float pitchHz() const { return pitchHz_; }
  const float* envelope() const { return envelope_; }
  int voicedFrames() const { return voicedFrames_; }

 private:
  void fft(float* re, float* im);

  float centre_[kBands] = {};
  float envelope_[kBands] = {};
  float pitchHz_ = 0.0f;
  int voicedFrames_ = 0;
  float* window_ = nullptr;
  float* cosTable_ = nullptr;
  float* sinTable_ = nullptr;
  float* melWeights_ = nullptr;
  uint16_t melFirst_[kBands] = {};
  uint16_t melCount_[kBands] = {};
  uint16_t melOffset_[kBands] = {};
  float* frame_ = nullptr;
  float* re_ = nullptr;
  float* im_ = nullptr;
  float* energyDb_ = nullptr;   // per frame
  float* bandsDb_ = nullptr;    // per frame x band
  float* pitches_ = nullptr;    // per voiced frame
};

}  // namespace cbb15
