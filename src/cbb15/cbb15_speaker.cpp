#include "cbb15_speaker.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

namespace cbb15 {

namespace {

constexpr int kSpectrumBins = SpeakerEncoder::kFftLength / 2 + 1;
constexpr float kPi = 3.14159265358979323846f;

double hzToMel(double hz) { return 1127.0 * log1p(hz / 700.0); }

template <typename T>
T* allocate(size_t n) {
  return static_cast<T*>(malloc(sizeof(T) * n));
}

}  // namespace

SpeakerEncoder::SpeakerEncoder() = default;

SpeakerEncoder::~SpeakerEncoder() {
  free(window_);
  free(cosTable_);
  free(sinTable_);
  free(melWeights_);
  free(frame_);
  free(re_);
  free(im_);
  free(energyDb_);
  free(bandsDb_);
  free(pitches_);
}

bool SpeakerEncoder::begin(const float* bandCentre) {
  memcpy(centre_, bandCentre, sizeof(centre_));
  if (window_ != nullptr) {
    return true;
  }
  window_ = allocate<float>(kFrameLength);
  cosTable_ = allocate<float>(kFftLength / 2);
  sinTable_ = allocate<float>(kFftLength / 2);
  frame_ = allocate<float>(kFrameLength);
  re_ = allocate<float>(kFftLength);
  im_ = allocate<float>(kFftLength);
  energyDb_ = allocate<float>(kMaxFrames);
  bandsDb_ = allocate<float>(kMaxFrames * kBands);
  pitches_ = allocate<float>(kMaxFrames);
  if (!window_ || !cosTable_ || !sinTable_ || !frame_ || !re_ || !im_ ||
      !energyDb_ || !bandsDb_ || !pitches_) {
    return false;
  }
  for (int n = 0; n < kFrameLength; ++n) {
    window_[n] = 0.5f - 0.5f * cosf(2.0f * kPi * n / kFrameLength);
  }
  for (int k = 0; k < kFftLength / 2; ++k) {
    cosTable_[k] = cosf(2.0f * kPi * k / kFftLength);
    sinTable_[k] = sinf(2.0f * kPi * k / kFftLength);
  }
  // Mel filter bank (same construction as tf.signal.linear_to_mel_weight_matrix).
  double edges[kBands + 2];
  const double lo = hzToMel(60.0), hi = hzToMel(7600.0);
  for (int i = 0; i < kBands + 2; ++i) {
    edges[i] = lo + (hi - lo) * i / (kBands + 1);
  }
  float packed[kSpectrumBins * 2];
  int used = 0;
  for (int m = 0; m < kBands; ++m) {
    melOffset_[m] = static_cast<uint16_t>(used);
    melCount_[m] = 0;
    for (int bin = 1; bin < kSpectrumBins; ++bin) {
      const double mel = hzToMel((kSampleRate / 2.0) * bin / (kSpectrumBins - 1));
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
  melWeights_ = allocate<float>(used);
  if (melWeights_ == nullptr) {
    return false;
  }
  memcpy(melWeights_, packed, sizeof(float) * used);
  return true;
}

void SpeakerEncoder::fft(float* re, float* im) {
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

bool SpeakerEncoder::encode(const int16_t* audio, size_t count, uint8_t* bits) {
  memset(bits, 0, kBits);
  voicedFrames_ = 0;
  pitchHz_ = 0.0f;
  if (count < static_cast<size_t>(kFrameLength)) {
    return false;
  }
  int frames = 1 + static_cast<int>((count - kFrameLength) / kFrameStep);
  if (frames > kMaxFrames) {
    frames = kMaxFrames;
  }
  int64_t sum = 0;
  for (size_t i = 0; i < count; ++i) {
    sum += audio[i];
  }
  const float mean = static_cast<float>(sum) / count;

  // Pass 1: band levels and frame energy of every frame.
  float maxEnergy = -1e30f;
  for (int f = 0; f < frames; ++f) {
    const int16_t* src = audio + f * kFrameStep;
    for (int n = 0; n < kFftLength; ++n) {
      re_[n] = n < kFrameLength ? (src[n] - mean) * window_[n] : 0.0f;
      im_[n] = 0.0f;
    }
    fft(re_, im_);
    float total = 0.0f;
    for (int k = 0; k < kSpectrumBins; ++k) {
      re_[k] = re_[k] * re_[k] + im_[k] * im_[k];  // power
      total += re_[k];
    }
    energyDb_[f] = 10.0f * log10f(total + 1e-9f);
    maxEnergy = energyDb_[f] > maxEnergy ? energyDb_[f] : maxEnergy;
    for (int m = 0; m < kBands; ++m) {
      float acc = 0.0f;
      const float* w = melWeights_ + melOffset_[m];
      for (int i = 0; i < melCount_[m]; ++i) {
        acc += re_[melFirst_[m] + i] * w[i];
      }
      bandsDb_[f * kBands + m] = 10.0f * log10f(acc + 1e-9f);
    }
  }

  // Pass 2: envelope and pitch over the voiced frames.
  float envelope[kBands] = {};
  int pitchCount = 0;
  const int lagMin = static_cast<int>(kSampleRate / kPitchMaxHz);  // 40
  const int lagMax = static_cast<int>(kSampleRate / kPitchMinHz);  // 228
  for (int f = 0; f < frames; ++f) {
    if (!(energyDb_[f] > maxEnergy - kVoicedRangeDb)) {
      continue;
    }
    ++voicedFrames_;
    for (int m = 0; m < kBands; ++m) {
      envelope[m] += bandsDb_[f * kBands + m];
    }
    const int16_t* src = audio + f * kFrameStep;
    for (int n = 0; n < kFrameLength; ++n) {
      frame_[n] = (src[n] - mean) * window_[n];
    }
    float ac0 = 0.0f;
    for (int n = 0; n < kFrameLength; ++n) {
      ac0 += frame_[n] * frame_[n];
    }
    float best = -1e30f;
    int bestLag = lagMin;
    for (int lag = lagMin; lag < lagMax; ++lag) {
      float ac = 0.0f;
      for (int n = 0; n + lag < kFrameLength; ++n) {
        ac += frame_[n] * frame_[n + lag];
      }
      if (ac > best) {
        best = ac;
        bestLag = lag;
      }
    }
    if (ac0 > 0.0f && best > 0.3f * ac0) {
      pitches_[pitchCount++] = static_cast<float>(kSampleRate) / bestLag;
    }
  }
  if (voicedFrames_ == 0) {
    return false;
  }
  float level = 0.0f;
  for (int m = 0; m < kBands; ++m) {
    envelope[m] /= voicedFrames_;
    level += envelope[m];
  }
  level /= kBands;
  for (int m = 0; m < kBands; ++m) {
    envelope_[m] = envelope[m] - level;
  }
  if (pitchCount > 0) {
    std::sort(pitches_, pitches_ + pitchCount);
    pitchHz_ = (pitchCount % 2) ? pitches_[pitchCount / 2]
                                : 0.5f * (pitches_[pitchCount / 2 - 1] +
                                          pitches_[pitchCount / 2]);
  }

  // Binary code.
  for (int m = 0; m < kBands; ++m) {
    int q = 0;  // number of edges below the value (edges -12..+12 dB)
    for (int e = -(kLevels / 2 - 1); e < kLevels / 2; ++e) {
      if (centre_[m] + e * kStepDb < envelope_[m]) {
        ++q;
      }
    }
    bits[m * kLevels + q] = 1;
    if (q > 0) {
      bits[m * kLevels + q - 1] = 1;
    }
  }
  if (pitchHz_ > 0.0f) {
    const float pos = log2f(pitchHz_ / kPitchMinHz) / log2f(kPitchMaxHz / kPitchMinHz);
    int i = static_cast<int>(lroundf(pos * (kPitchBins - 1)));
    i = std::max(0, std::min(kPitchBins - 1, i));
    const int base = kBands * kLevels;
    for (int copy = 0; copy < 2; ++copy) {
      for (int j = i - 1; j <= i + 1; ++j) {
        if (j >= 0 && j < kPitchBins) {
          bits[base + copy * kPitchBins + j] = 1;
        }
      }
    }
  }
  return true;
}

}  // namespace cbb15
