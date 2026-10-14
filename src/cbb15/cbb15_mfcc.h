// BrainBoard1500 - MFCC front end of the BrainChip Dev Hub speech_commands model.
//
// Reproduces akida1/model_zoo/speech_commands/speech_commands_data_loader.py
// (MLPerf Tiny parameters) without TensorFlow:
//   16 kHz, 1 s (16000 samples), peak normalisation to [-1, 1],
//   periodic Hann window of 480 samples (30 ms), hop 320 (20 ms),
//   512-point FFT magnitude, 40 mel bins from 20 to 4000 Hz
//   (tf.signal.linear_to_mel_weight_matrix, HTK mel scale),
//   log(mel + 1e-6), first 10 coefficients of
//   tf.signal.mfccs_from_log_mel_spectrograms (DCT-II / sqrt(2 * 40)),
//   then the uint8 scaling used in training: clip to [-51.4311, 14.2340]
//   (0.5 / 99.5 percentiles printed in the Dev Hub notebooks) and map to
//   0..255.
// The output is the model input tensor [49][10][1].
//
// tools/cbb15_mfcc.py is the NumPy reference of the same computation.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cbb15 {

class Mfcc {
 public:
  static constexpr int kSampleRate = 16000;
  static constexpr int kSamples = 16000;
  static constexpr int kFrameLength = 480;
  static constexpr int kFrameStep = 320;
  static constexpr int kFftLength = 512;
  static constexpr int kMelBins = 40;
  static constexpr int kCoefficients = 10;
  static constexpr int kFrames = 1 + (kSamples - kFrameLength) / kFrameStep;  // 49
  static constexpr int kOutputSize = kFrames * kCoefficients;  // 490
  static constexpr float kRangeMin = -51.4311f;
  static constexpr float kRangeMax = 14.2340f;

  Mfcc();
  ~Mfcc();
  // Builds the window, FFT, mel and DCT tables (about 10 KB of RAM).
  bool begin();

  // audio: kSamples signed 16-bit samples (shorter input is zero padded).
  // removeDc subtracts the mean first; the Nicla Vision PDM microphone has
  // a DC offset of a few hundred counts that is not present in the training
  // data. out: kOutputSize uint8 values. floatOut (optional): the unscaled
  // MFCC matrix.
  void compute(const int16_t* audio, size_t count, uint8_t* out,
               bool removeDc = true, float* floatOut = nullptr);

 private:
  void fft(float* re, float* im);

  float* window_ = nullptr;      // kFrameLength
  float* cosTable_ = nullptr;    // kFftLength / 2
  float* sinTable_ = nullptr;    // kFftLength / 2
  float* dct_ = nullptr;         // kCoefficients * kMelBins
  float* melWeights_ = nullptr;  // packed triangle weights
  uint16_t melFirst_[kMelBins] = {};
  uint16_t melCount_[kMelBins] = {};
  uint16_t melOffset_[kMelBins] = {};
  float* re_ = nullptr;
  float* im_ = nullptr;
};

}  // namespace cbb15
