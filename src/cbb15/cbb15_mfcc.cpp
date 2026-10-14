#include "cbb15_mfcc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace cbb15 {

namespace {

constexpr int kSpectrumBins = Mfcc::kFftLength / 2 + 1;  // 257
constexpr float kPi = 3.14159265358979323846f;

double hzToMel(double hz) { return 1127.0 * log1p(hz / 700.0); }

}  // namespace

Mfcc::Mfcc() = default;

Mfcc::~Mfcc() {
  free(window_);
  free(cosTable_);
  free(sinTable_);
  free(dct_);
  free(melWeights_);
  free(re_);
  free(im_);
}

bool Mfcc::begin() {
  if (window_ != nullptr) {
    return true;
  }
  window_ = static_cast<float*>(malloc(sizeof(float) * kFrameLength));
  cosTable_ = static_cast<float*>(malloc(sizeof(float) * kFftLength / 2));
  sinTable_ = static_cast<float*>(malloc(sizeof(float) * kFftLength / 2));
  dct_ = static_cast<float*>(malloc(sizeof(float) * kCoefficients * kMelBins));
  re_ = static_cast<float*>(malloc(sizeof(float) * kFftLength));
  im_ = static_cast<float*>(malloc(sizeof(float) * kFftLength));
  if (!window_ || !cosTable_ || !sinTable_ || !dct_ || !re_ || !im_) {
    return false;
  }

  // Periodic Hann window (tf.signal.hann_window, periodic=True).
  for (int n = 0; n < kFrameLength; ++n) {
    window_[n] = 0.5f - 0.5f * cosf(2.0f * kPi * n / kFrameLength);
  }
  for (int k = 0; k < kFftLength / 2; ++k) {
    cosTable_[k] = cosf(2.0f * kPi * k / kFftLength);
    sinTable_[k] = sinf(2.0f * kPi * k / kFftLength);
  }
  // DCT-II without normalisation (2 * sum x cos(...)), scaled by
  // 1 / sqrt(2 * kMelBins) as in tf.signal.mfccs_from_log_mel_spectrograms.
  const float scale = 1.0f / sqrtf(2.0f * kMelBins);
  for (int k = 0; k < kCoefficients; ++k) {
    for (int m = 0; m < kMelBins; ++m) {
      dct_[k * kMelBins + m] =
          2.0f * cosf(kPi * k * (2 * m + 1) / (2.0f * kMelBins)) * scale;
    }
  }

  // Mel filter bank, tf.signal.linear_to_mel_weight_matrix: the edges are
  // equally spaced on the mel scale, the spectrum bins are 0..8000 Hz in
  // 257 steps, and the DC bin always gets weight 0. Only the non-zero part
  // of every triangle is stored.
  double edges[kMelBins + 2];
  const double lo = hzToMel(20.0), hi = hzToMel(4000.0);
  for (int i = 0; i < kMelBins + 2; ++i) {
    edges[i] = lo + (hi - lo) * i / (kMelBins + 1);
  }
  float packed[kSpectrumBins * 2];
  int used = 0;
  for (int m = 0; m < kMelBins; ++m) {
    melFirst_[m] = 0;
    melCount_[m] = 0;
    melOffset_[m] = static_cast<uint16_t>(used);
    for (int bin = 1; bin < kSpectrumBins; ++bin) {
      const double hz = (kSampleRate / 2.0) * bin / (kSpectrumBins - 1);
      const double mel = hzToMel(hz);
      const double lower = (mel - edges[m]) / (edges[m + 1] - edges[m]);
      const double upper = (edges[m + 2] - mel) / (edges[m + 2] - edges[m + 1]);
      const double w = fmax(0.0, fmin(lower, upper));
      if (w > 0.0) {
        if (melCount_[m] == 0) {
          melFirst_[m] = static_cast<uint16_t>(bin);
        }
        packed[used++] = static_cast<float>(w);
        ++melCount_[m];
      }
    }
  }
  melWeights_ = static_cast<float*>(malloc(sizeof(float) * used));
  if (melWeights_ == nullptr) {
    return false;
  }
  memcpy(melWeights_, packed, sizeof(float) * used);
  return true;
}

void Mfcc::fft(float* re, float* im) {
  // Iterative radix-2 decimation-in-time FFT, kFftLength points.
  const int n = kFftLength;
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      float t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const int half = len >> 1;
    const int step = n / len;
    for (int i = 0; i < n; i += len) {
      for (int k = 0; k < half; ++k) {
        const float wr = cosTable_[k * step];
        const float wi = -sinTable_[k * step];
        const int a = i + k, b = a + half;
        const float tr = re[b] * wr - im[b] * wi;
        const float ti = re[b] * wi + im[b] * wr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
      }
    }
  }
}

void Mfcc::compute(const int16_t* audio, size_t count, uint8_t* out,
                   bool removeDc, float* floatOut) {
  if (count > static_cast<size_t>(kSamples)) {
    count = kSamples;
  }
  // DC offset and peak for the normalisation to [-1, 1].
  float mean = 0.0f;
  if (removeDc && count > 0) {
    int64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
      sum += audio[i];
    }
    mean = static_cast<float>(sum) / count;
  }
  float peak = 0.0f;
  for (size_t i = 0; i < count; ++i) {
    const float v = fabsf(audio[i] - mean);
    peak = v > peak ? v : peak;
  }
  // Zero padding beyond `count` is also offset-free after the subtraction.
  const float gain = 1.0f / (peak + 1e-9f);

  float mel[kMelBins];
  for (int f = 0; f < kFrames; ++f) {
    const int start = f * kFrameStep;
    for (int n = 0; n < kFftLength; ++n) {
      float v = 0.0f;
      if (n < kFrameLength) {
        const int idx = start + n;
        const float s = idx < static_cast<int>(count) ? (audio[idx] - mean) : 0.0f;
        v = s * gain * window_[n];
      }
      re_[n] = v;
      im_[n] = 0.0f;
    }
    fft(re_, im_);
    // Magnitude spectrum is reused in place (bins 0..256).
    for (int k = 0; k < kSpectrumBins; ++k) {
      re_[k] = sqrtf(re_[k] * re_[k] + im_[k] * im_[k]);
    }
    for (int m = 0; m < kMelBins; ++m) {
      float acc = 0.0f;
      const float* w = melWeights_ + melOffset_[m];
      for (int i = 0; i < melCount_[m]; ++i) {
        acc += re_[melFirst_[m] + i] * w[i];
      }
      mel[m] = logf(acc + 1e-6f);
    }
    for (int k = 0; k < kCoefficients; ++k) {
      float c = 0.0f;
      const float* d = dct_ + k * kMelBins;
      for (int m = 0; m < kMelBins; ++m) {
        c += mel[m] * d[m];
      }
      if (floatOut != nullptr) {
        floatOut[f * kCoefficients + k] = c;
      }
      if (c < kRangeMin) c = kRangeMin;
      if (c > kRangeMax) c = kRangeMax;
      const float q = (c - kRangeMin) * 255.0f / (kRangeMax - kRangeMin);
      out[f * kCoefficients + k] = static_cast<uint8_t>(lroundf(q));
    }
  }
}

}  // namespace cbb15
