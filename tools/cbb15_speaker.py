"""NumPy reference of the BrainBoard1500 speaker encoder (voice learning example).

A keyword-spotting network is trained to ignore who is speaking, so its
features are a poor basis for recognising a voice. The voice learning
example therefore describes an utterance with two classic speaker cues and
lets the AKD1500 learn on a binary code of them:

  1. spectral envelope: mean level (dB) of 32 mel bands from 60 to 7600 Hz
     over the voiced frames, minus its own mean (independent of loudness)
  2. pitch: median fundamental frequency of the voiced frames
     (autocorrelation, 70..400 Hz)

Binary code (304 bits), the input of the Akida learning layer:
  - every band: 8 levels, edges at centre[b] + (-12, -8, ..., +12) dB;
    the level bit and the one below it are set (2 of 8 bits)
  - pitch: 24 log-spaced bins from 70 to 400 Hz, three adjacent bits set,
    repeated twice to give pitch a weight similar to 3 bands

The C++ implementation is src/cbb15/cbb15_speaker.cpp.
"""
import numpy as np

import cbb15_mfcc

SAMPLE_RATE = 16000
FRAME_LENGTH = 480
FRAME_STEP = 320
FFT_LENGTH = 512
NUM_BANDS = 32
LOWER_HZ = 60.0
UPPER_HZ = 7600.0
VOICED_RANGE_DB = 25.0
LEVELS = 8
STEP_DB = 4.0
PITCH_BINS = 24
PITCH_MIN_HZ = 70.0
PITCH_MAX_HZ = 400.0
PITCH_MIN_CORRELATION = 0.3
NUM_BITS = NUM_BANDS * LEVELS + 2 * PITCH_BINS  # 304

MEL = cbb15_mfcc.mel_weight_matrix(NUM_BANDS, FFT_LENGTH // 2 + 1, SAMPLE_RATE,
                                   LOWER_HZ, UPPER_HZ)


def analyse(audio_int16):
    """Returns (envelope[32] in dB, median pitch in Hz or 0)."""
    x = np.asarray(audio_int16, dtype=np.float64)
    x = x - x.mean()
    n = 1 + (len(x) - FRAME_LENGTH) // FRAME_STEP
    window = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(FRAME_LENGTH) / FRAME_LENGTH)
    frames = np.stack([x[i * FRAME_STEP:i * FRAME_STEP + FRAME_LENGTH] * window
                       for i in range(n)])
    power = np.abs(np.fft.rfft(frames, FFT_LENGTH)) ** 2
    energy = 10 * np.log10(power.sum(axis=1) + 1e-9)
    voiced = energy > energy.max() - VOICED_RANGE_DB
    bands = 10 * np.log10(power[voiced] @ MEL + 1e-9)
    envelope = bands.mean(axis=0)
    envelope -= envelope.mean()
    lo = int(SAMPLE_RATE // PITCH_MAX_HZ)
    hi = int(SAMPLE_RATE // PITCH_MIN_HZ)
    pitches = []
    for f in frames[voiced]:
        ac = np.correlate(f, f, 'full')[FRAME_LENGTH - 1:]
        k = lo + int(np.argmax(ac[lo:hi]))
        if ac[0] > 0 and ac[k] > PITCH_MIN_CORRELATION * ac[0]:
            pitches.append(SAMPLE_RATE / k)
    return envelope, (float(np.median(pitches)) if pitches else 0.0)


def encode(envelope, pitch, centre):
    bits = np.zeros(NUM_BITS, dtype=np.uint8)
    offsets = np.arange(-(LEVELS // 2 - 1), LEVELS // 2) * STEP_DB  # 7 edges
    for b in range(NUM_BANDS):
        q = int(np.searchsorted(centre[b] + offsets, envelope[b]))
        bits[b * LEVELS + q] = 1
        if q > 0:
            bits[b * LEVELS + q - 1] = 1
    if pitch > 0:
        pos = np.log2(pitch / PITCH_MIN_HZ) / np.log2(PITCH_MAX_HZ / PITCH_MIN_HZ)
        i = int(np.clip(np.round(pos * (PITCH_BINS - 1)), 0, PITCH_BINS - 1))
        base = NUM_BANDS * LEVELS
        for copy in range(2):
            for j in (i - 1, i, i + 1):
                if 0 <= j < PITCH_BINS:
                    bits[base + copy * PITCH_BINS + j] = 1
    return bits
