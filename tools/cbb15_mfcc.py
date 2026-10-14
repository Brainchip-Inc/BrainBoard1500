"""NumPy reference of the Dev Hub speech_commands MFCC front end.

This mirrors akida1/model_zoo/speech_commands/speech_commands_data_loader.py
(BrainChip Dev Hub, Apache 2.0) without TensorFlow, so that the C++
implementation in src/cbb15/cbb15_mfcc.cpp can be checked against it:

    16 kHz, 1 s clip, peak normalisation, Hann window 480 (30 ms),
    hop 320 (20 ms), FFT 512, magnitude spectrum, 40 mel bins 20-4000 Hz
    (tf.signal.linear_to_mel_weight_matrix), log(mel + 1e-6), first 10
    coefficients of tf.signal.mfccs_from_log_mel_spectrograms, then the
    uint8 scaling with the training range printed in the Dev Hub notebooks
    (0.5-99.5 percentile): -51.4311 ... 14.2340.
"""
import numpy as np

SAMPLE_RATE = 16000
DESIRED_SAMPLES = 16000
FRAME_LENGTH = 480
FRAME_STEP = 320
FFT_LENGTH = 512
NUM_MEL_BINS = 40
NUM_MFCC = 10
MEL_LOWER_HZ = 20.0
MEL_UPPER_HZ = 4000.0
NUM_FRAMES = 1 + (DESIRED_SAMPLES - FRAME_LENGTH) // FRAME_STEP  # 49
MFCC_MIN = -51.4311
MFCC_MAX = 14.2340


def hz_to_mel(f):
    return 1127.0 * np.log1p(np.asarray(f, dtype=np.float64) / 700.0)


def mel_weight_matrix(num_mel_bins=NUM_MEL_BINS, num_spectrogram_bins=FFT_LENGTH // 2 + 1,
                      sample_rate=SAMPLE_RATE, lower=MEL_LOWER_HZ, upper=MEL_UPPER_HZ):
    """Same algorithm as tf.signal.linear_to_mel_weight_matrix (HTK mel scale)."""
    nyquist = sample_rate / 2.0
    linear_freqs = np.linspace(0.0, nyquist, num_spectrogram_bins)[1:]
    spec_mel = hz_to_mel(linear_freqs)[:, None]
    edges = np.linspace(hz_to_mel(lower), hz_to_mel(upper), num_mel_bins + 2)
    lower_e, center_e, upper_e = edges[:-2], edges[1:-1], edges[2:]
    lower_slopes = (spec_mel - lower_e) / (center_e - lower_e)
    upper_slopes = (upper_e - spec_mel) / (upper_e - center_e)
    w = np.maximum(0.0, np.minimum(lower_slopes, upper_slopes))
    return np.pad(w, [[1, 0], [0, 0]])  # the DC bin gets zero weight


def mfcc_float(audio_int16):
    """Returns the float MFCC matrix [NUM_FRAMES, NUM_MFCC]."""
    x = np.zeros(DESIRED_SAMPLES, dtype=np.float64)
    a = np.asarray(audio_int16, dtype=np.float64)[:DESIRED_SAMPLES]
    x[:len(a)] = a
    x = x / (np.max(np.abs(x)) + 1e-9)
    n = np.arange(FRAME_LENGTH)
    window = 0.5 - 0.5 * np.cos(2.0 * np.pi * n / FRAME_LENGTH)  # periodic Hann
    frames = np.stack([x[i * FRAME_STEP:i * FRAME_STEP + FRAME_LENGTH] * window
                       for i in range(NUM_FRAMES)])
    mag = np.abs(np.fft.rfft(frames, n=FFT_LENGTH))
    mel = mag @ mel_weight_matrix()
    log_mel = np.log(mel + 1e-6)
    k = np.arange(NUM_MFCC)[:, None]
    m = np.arange(NUM_MEL_BINS)[None, :]
    dct = 2.0 * np.cos(np.pi * k * (2 * m + 1) / (2.0 * NUM_MEL_BINS))  # DCT-II, unnormalised
    return (log_mel @ dct.T) / np.sqrt(2.0 * NUM_MEL_BINS)


def mfcc_uint8(audio_int16):
    """Returns the model input [NUM_FRAMES, NUM_MFCC, 1] as uint8."""
    f = np.clip(mfcc_float(audio_int16), MFCC_MIN, MFCC_MAX)
    q = np.round((f - MFCC_MIN) * 255.0 / (MFCC_MAX - MFCC_MIN))
    return q.astype(np.uint8)[:, :, None]


LABELS = ["Down", "Go", "Left", "No", "Off", "On", "Right", "Stop", "Up", "Yes",
          "Silence", "Unknown"]
